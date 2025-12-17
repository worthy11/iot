import tkinter as tk
from tkinter import ttk, messagebox
import socket
import paho.mqtt.client as mqtt
import threading
import time
import json
import os
from datetime import datetime, timedelta

# Configuration
BROKER = "10.177.164.196"
PORT = 1883
USER_ID = "f8e87394"
TOPIC_SUBSCRIPTION = f"{USER_ID}/+/data/#"
DEFAULT_MAC = "1C:69:20:A3:8F:F0"
CONFIG_FILE = "aquatest_config.json"

class IoTApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Aquatest Controller")
        self.root.geometry("800x700")  # Wider window for two columns
        
        # Initialize attributes that might be accessed by callbacks early
        self.feeding_interval_sec = None  # None means not set or disabled
        self.last_feed_timestamp = None
        self.last_feed_time_formatted = None  # Formatted as "MM/DD HH:MM"
        self.config_loading = True
        
        # Style
        style = ttk.Style()
        style.configure("TLabel", font=("Segoe UI", 11))
        style.configure("Header.TLabel", font=("Segoe UI", 14, "bold"))
        style.configure("Card.TLabel", font=("Segoe UI", 10, "bold"), foreground="#666666")
        style.configure("Value.TLabel", font=("Segoe UI", 24))
        
        # Main container with padding
        main_frame = ttk.Frame(root, padding="20")
        main_frame.pack(fill=tk.BOTH, expand=True)

        # --- HEADER & INFO ---
        self.local_ip = self.get_local_ip()
        header_frame = ttk.Frame(main_frame)
        header_frame.pack(fill=tk.X, pady=(0, 10))
        
        ttk.Label(header_frame, text="Aquatest", style="Header.TLabel").pack(side=tk.LEFT)
        ttk.Label(header_frame, text=f"IP: {self.local_ip}", foreground="blue").pack(side=tk.RIGHT)

        # Create two column layout
        left_column = ttk.Frame(main_frame)
        left_column.pack(side=tk.LEFT, fill=tk.BOTH, expand=False, padx=(0, 10))
        
        right_column = ttk.Frame(main_frame)
        right_column.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        # --- LEFT COLUMN: Live Monitor ---
        monitor_frame = ttk.LabelFrame(left_column, text="Live Monitor", padding="10")
        monitor_frame.pack(fill=tk.BOTH, expand=True, pady=5)

        self.temp_var = tk.StringVar(value="--")
        self.create_card(monitor_frame, "TEMPERATURE", self.temp_var, "°C")

        self.ph_var = tk.StringVar(value="--")
        self.create_card(monitor_frame, "PH LEVEL", self.ph_var, "")

        self.feed_var = tk.StringVar(value="--:--:--")
        self.create_card(monitor_frame, "LAST FEED", self.feed_var, "")
        
        self.next_feed_var = tk.StringVar(value="--:--:--")
        self.create_card(monitor_frame, "NEXT FEED", self.next_feed_var, "")

        # --- RIGHT COLUMN: Configuration and Control ---
        # Configuration Display Section
        config_frame = ttk.LabelFrame(right_column, text="Current Configuration", padding="10")
        config_frame.pack(fill=tk.X, pady=5)

        self.config_mac_var = tk.StringVar(value="--")
        self.config_temp_var = tk.StringVar(value="--")
        self.config_feed_var = tk.StringVar(value="--")
        self.config_last_feed_var = tk.StringVar(value="--")

        self.create_config_row(config_frame, "MAC Address:", self.config_mac_var)
        self.create_config_row(config_frame, "Temp Interval:", self.config_temp_var, "s")
        self.create_config_row(config_frame, "Feed Interval:", self.config_feed_var, "s")
        self.create_config_row(config_frame, "Last Feed:", self.config_last_feed_var)

        control_frame = ttk.LabelFrame(right_column, text="Control Panel", padding="10")
        control_frame.pack(fill=tk.BOTH, expand=True, pady=10)

        mac_frame = ttk.Frame(control_frame)
        mac_frame.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(mac_frame, text="Target MAC:", width=12).pack(side=tk.LEFT)
        self.mac_var = tk.StringVar(value=DEFAULT_MAC)
        self.mac_var.trace_add('write', lambda *args: (self.save_config(), self.update_config_display()))
        ttk.Entry(mac_frame, textvariable=self.mac_var).pack(side=tk.LEFT, fill=tk.X, expand=True)

        # Temperature interval setting
        cmd_temp = ttk.Frame(control_frame)
        cmd_temp.pack(fill=tk.X, pady=5)
        ttk.Label(cmd_temp, text="Set Temp (s):", width=12).pack(side=tk.LEFT)
        self.set_temp_var = tk.StringVar(value="60")
        ttk.Entry(cmd_temp, textvariable=self.set_temp_var, width=10).pack(side=tk.LEFT, padx=5)
        ttk.Button(cmd_temp, text="Set", command=self.send_set_temp).pack(side=tk.LEFT)

        # Feeding interval setting
        cmd_feed = ttk.Frame(control_frame)
        cmd_feed.pack(fill=tk.X, pady=5)
        ttk.Label(cmd_feed, text="Set Feed (s):", width=12).pack(side=tk.LEFT)
        self.set_feed_var = tk.StringVar(value="3600")
        ttk.Entry(cmd_feed, textvariable=self.set_feed_var, width=10).pack(side=tk.LEFT, padx=5)
        ttk.Button(cmd_feed, text="Set", command=self.send_set_feed).pack(side=tk.LEFT)
        
        # Update config display initially (after all variables are created)
        self.update_config_display()

        ttk.Separator(control_frame, orient="horizontal").pack(fill=tk.X, pady=10)

        # Force action buttons
        force_frame = ttk.LabelFrame(control_frame, text="Force Actions", padding="5")
        force_frame.pack(fill=tk.X, pady=5)
        
        btn_frame1 = ttk.Frame(force_frame)
        btn_frame1.pack(fill=tk.X, pady=2)
        ttk.Button(btn_frame1, text="Force Temp", command=self.send_force_temp).pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(0, 2))
        ttk.Button(btn_frame1, text="Force pH", command=self.send_force_ph).pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(2, 0))
        
        btn_frame2 = ttk.Frame(force_frame)
        btn_frame2.pack(fill=tk.X, pady=2)
        ttk.Button(btn_frame2, text="Force Feed", command=self.send_force_feed).pack(side=tk.LEFT, expand=True, fill=tk.X)

        # --- STATUS BAR ---
        self.status_var = tk.StringVar(value="Connecting to MQTT...")
        status_bar = ttk.Label(root, textvariable=self.status_var, relief=tk.SUNKEN, anchor=tk.W, font=("Segoe UI", 9))
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)
        
        # MQTT Client setup
        self.client = mqtt.Client()
        # self.client.username_pw_set(USER_ID) # Removed as it prevents connection
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        
        self.connected = False
        
        # Load saved configuration (before starting MQTT to restore values)
        self.load_config()
        
        # Start MQTT in a separate thread
        self.mqtt_thread = threading.Thread(target=self.start_mqtt, daemon=True)
        self.mqtt_thread.start()
        
        # Mark initialization complete
        self.config_loading = False

    def create_card(self, parent, title, value_var, unit):
        card = ttk.Frame(parent)
        card.pack(fill=tk.X, pady=2)
        
        row = ttk.Frame(card)
        row.pack(fill=tk.X)
        
        ttk.Label(row, text=title, style="Card.TLabel", width=15).pack(side=tk.LEFT, anchor=tk.W)
        ttk.Label(row, textvariable=value_var, style="Value.TLabel").pack(side=tk.LEFT)
        if unit:
            ttk.Label(row, text=unit, font=("Segoe UI", 12), padding=(2, 5, 0, 0)).pack(side=tk.LEFT)
    
    def create_config_row(self, parent, label, value_var, unit=""):
        """Create a configuration display row"""
        row = ttk.Frame(parent)
        row.pack(fill=tk.X, pady=2)
        
        ttk.Label(row, text=label, width=15, anchor=tk.W).pack(side=tk.LEFT)
        ttk.Label(row, textvariable=value_var, font=("Segoe UI", 10, "bold"), foreground="#2980b9").pack(side=tk.LEFT, padx=(5, 0))
        if unit:
            ttk.Label(row, text=unit, font=("Segoe UI", 9)).pack(side=tk.LEFT, padx=(3, 0))
    
    def update_config_display(self):
        """Update the configuration display section"""
        self.config_mac_var.set(self.mac_var.get() or "--")
        
        temp_val = self.set_temp_var.get().strip()
        if temp_val:
            try:
                temp_int = int(temp_val)
                self.config_temp_var.set(str(temp_int) if temp_int > 0 else "Disabled")
            except:
                self.config_temp_var.set(temp_val)
        else:
            self.config_temp_var.set("--")
        
        feed_val = self.set_feed_var.get().strip()
        if feed_val:
            try:
                feed_int = int(feed_val)
                self.config_feed_var.set(str(feed_int) if feed_int > 0 else "Disabled")
            except:
                self.config_feed_var.set(feed_val)
        else:
            self.config_feed_var.set("--")
        
        if hasattr(self, 'last_feed_time_formatted') and self.last_feed_time_formatted:
            self.config_last_feed_var.set(self.last_feed_time_formatted)
        else:
            self.config_last_feed_var.set("Never")

    def get_local_ip(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            # Doesn't need to be reachable
            s.connect(('10.255.255.255', 1))
            IP = s.getsockname()[0]
        except Exception:
            IP = '127.0.0.1'
        finally:
            s.close()
        return IP

    def start_mqtt(self):
        try:
            self.client.connect(BROKER, PORT, 60)
            self.client.loop_forever()
        except Exception as e:
            self.update_status(f"Error: {str(e)}")

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            self.connected = True
            self.update_status(f"Connected to {BROKER}")
            client.subscribe(TOPIC_SUBSCRIPTION)
        else:
            self.update_status(f"Connection failed code {rc}")

    def on_message(self, client, userdata, msg):
        try:
            payload = msg.payload.decode()
            topic = msg.topic
            # Topic format: user_id/mac/data/type
            parts = topic.split('/')
            
            if len(parts) >= 4 and parts[2] == 'data':
                data_type = parts[3]
                
                # Parse message format: value,timestamp or timestamp,time,status
                if data_type == 'temperature':
                    # Format: "value,timestamp"
                    try:
                        parts_temp = payload.split(',')
                        value = parts_temp[0]
                        timestamp_str = ""
                        if len(parts_temp) >= 2:
                            try:
                                timestamp = int(parts_temp[1])
                                timestamp_dt = datetime.fromtimestamp(timestamp)
                                timestamp_str = timestamp_dt.strftime(" (%H:%M:%S)")
                            except:
                                pass
                        self.root.after(0, lambda v=value, ts=timestamp_str: self.temp_var.set(f"{float(v):.2f}{ts}"))
                    except:
                        self.root.after(0, lambda: self.temp_var.set(payload))
                elif data_type == 'ph':
                    # Format: "value,timestamp"
                    try:
                        parts_ph = payload.split(',')
                        value = parts_ph[0]
                        timestamp_str = ""
                        if len(parts_ph) >= 2:
                            try:
                                timestamp = int(parts_ph[1])
                                timestamp_dt = datetime.fromtimestamp(timestamp)
                                timestamp_str = timestamp_dt.strftime(" (%H:%M:%S)")
                            except:
                                pass
                        self.root.after(0, lambda v=value, ts=timestamp_str: self.ph_var.set(f"{float(v):.2f}{ts}"))
                    except:
                        self.root.after(0, lambda: self.ph_var.set(payload))
                elif data_type == 'feed':
                    # Format: "timestamp,MM/DD HH:MM,status"
                    try:
                        parts_feed = payload.split(',')
                        if len(parts_feed) >= 1:
                            # Update last feed timestamp and formatted time
                            try:
                                timestamp = int(parts_feed[0])
                                self.last_feed_timestamp = timestamp
                            except:
                                pass
                            
                            # Extract status (success/failure)
                            status_str = ""
                            if len(parts_feed) >= 3:
                                status = parts_feed[2].strip().lower()
                                if status == "success":
                                    status_str = " ✓"
                                elif status == "failure":
                                    status_str = " ✗"
                            
                            if len(parts_feed) >= 2:
                                # Extract MM/DD HH:MM format
                                time_str = parts_feed[1]  # MM/DD HH:MM
                                self.last_feed_time_formatted = time_str
                                # Display time part (HH:MM) with status in the monitor
                                if ' ' in time_str:
                                    time_only = time_str.split(' ')[1]  # Extract HH:MM
                                    display_text = f"{time_only}{status_str}"
                                    self.root.after(0, lambda t=display_text: self.feed_var.set(t))
                                else:
                                    display_text = f"{time_str}{status_str}"
                                    self.root.after(0, lambda t=display_text: self.feed_var.set(t))
                            else:
                                display_text = f"{payload}{status_str}"
                                self.root.after(0, lambda t=display_text: self.feed_var.set(t))
                            
                            # Update next feed time display
                            self.root.after(0, self.calculate_and_update_next_feed)
                            # Save config with updated last feed time
                            self.root.after(0, self.save_config)
                            # Update config display
                            self.root.after(0, self.update_config_display)
                    except:
                        self.root.after(0, lambda: self.feed_var.set(payload))
                    
        except Exception as e:
            print(f"Error parsing message: {e}")

    def update_status(self, text):
        self.root.after(0, lambda: self.status_var.set(text))

    # --- COMMAND FUNCTIONS ---
    def publish_command(self, payload):
        if not self.connected:
            messagebox.showwarning("Not Connected", "Waiting for connection to MQTT broker...")
            return

        mac = self.mac_var.get().strip()
        if not mac:
            messagebox.showwarning("Input Error", "Please enter a valid MAC address.")
            return

        topic = f"{USER_ID}/{mac}/cmd"
        
        try:
            self.client.publish(topic, payload)
            self.update_status(f"Sent: '{payload}' to {topic}")
        except Exception as e:
            self.update_status(f"Publish Error: {e}")
            messagebox.showerror("Error", f"Failed to publish: {e}")

    def send_set_temp(self):
        val = self.set_temp_var.get().strip()
        if val:
            try:
                interval = int(val)
                if interval >= 0:
                    self.publish_command(f"set temp {interval}")
                    # Save config after setting interval
                    self.save_config()
                    # Update config display
                    self.update_config_display()
                else:
                    messagebox.showwarning("Invalid Value", "Interval must be >= 0 (0 to disable)")
            except ValueError:
                messagebox.showwarning("Invalid Value", "Please enter a valid number")

    def send_set_feed(self):
        val = self.set_feed_var.get().strip()
        if val:
            try:
                interval = int(val)
                if interval >= 0:
                    # Update local feeding interval
                    self.feeding_interval_sec = interval if interval > 0 else None
                    self.publish_command(f"set feed {interval}")
                    # Calculate and update next feed time
                    self.root.after(0, self.calculate_and_update_next_feed)
                    # Save config after setting interval
                    self.save_config()
                    # Update config display
                    self.update_config_display()
                else:
                    messagebox.showwarning("Invalid Value", "Interval must be >= 0 (0 to disable)")
            except ValueError:
                messagebox.showwarning("Invalid Value", "Please enter a valid number")

    def send_force_temp(self):
        self.publish_command("force temp")

    def send_force_ph(self):
        self.publish_command("force ph")

    def send_force_feed(self):
        self.publish_command("force feed")
    
    def calculate_and_update_next_feed(self):
        """Calculate next feed time based on interval and last feed time"""
        if self.feeding_interval_sec is None or self.feeding_interval_sec == 0:
            self.next_feed_var.set("Disabled")
            return
        
        current_time = time.time()
        
        # If we have a last feed timestamp, calculate from that
        # Otherwise, calculate from current time
        if self.last_feed_timestamp is not None:
            next_feed_time = self.last_feed_timestamp + self.feeding_interval_sec
        else:
            # No previous feed, calculate from now
            next_feed_time = current_time + self.feeding_interval_sec
        
        # Format the next feed time
        next_feed_dt = datetime.fromtimestamp(next_feed_time)
        time_str = next_feed_dt.strftime("%H:%M:%S")
        self.next_feed_var.set(time_str)
    
    def load_config(self):
        """Load configuration from JSON file"""
        if os.path.exists(CONFIG_FILE):
            try:
                with open(CONFIG_FILE, 'r') as f:
                    config = json.load(f)
                    
                # Load MAC address
                if 'mac_address' in config:
                    self.mac_var.set(config['mac_address'])
                
                # Load temperature interval
                if 'temp_interval_sec' in config:
                    self.set_temp_var.set(str(config['temp_interval_sec']))
                
                # Load feeding interval
                if 'feed_interval_sec' in config:
                    feed_interval = config['feed_interval_sec']
                    self.set_feed_var.set(str(feed_interval))
                    self.feeding_interval_sec = feed_interval if feed_interval > 0 else None
                
                # Load last feed time if available
                if 'last_feed_time' in config and config['last_feed_time'] is not None:
                    # Load formatted time string (MM/DD HH:MM)
                    self.last_feed_time_formatted = config['last_feed_time']
                    # Try to parse it back to timestamp for calculations
                    try:
                        # Parse MM/DD HH:MM format - we need to reconstruct full datetime
                        # For simplicity, we'll use current year and parse the format
                        if self.last_feed_time_formatted:
                            parts = self.last_feed_time_formatted.split()
                            if len(parts) == 2:
                                date_part, time_part = parts
                                month_day = date_part.split('/')
                                hour_min = time_part.split(':')
                                if len(month_day) == 2 and len(hour_min) == 2:
                                    current_year = datetime.now().year
                                    dt = datetime(current_year, int(month_day[0]), int(month_day[1]), 
                                                 int(hour_min[0]), int(hour_min[1]))
                                    self.last_feed_timestamp = int(dt.timestamp())
                    except:
                        pass
                elif 'last_feed_timestamp' in config and config['last_feed_timestamp'] is not None:
                    # Legacy support: if old timestamp format exists, convert it
                    try:
                        timestamp = config['last_feed_timestamp']
                        self.last_feed_timestamp = timestamp
                        feed_dt = datetime.fromtimestamp(timestamp)
                        self.last_feed_time_formatted = feed_dt.strftime("%m/%d %H:%M")
                    except:
                        pass
                
                # Update config display after loading
                self.root.after(150, self.update_config_display)
                
                print(f"Configuration loaded from {CONFIG_FILE}")
            except Exception as e:
                print(f"Error loading config: {e}")
        else:
            print(f"Config file {CONFIG_FILE} not found, using defaults")
            # Update config display with defaults
            self.root.after(100, self.update_config_display)
    
    def save_config(self):
        """Save current configuration to JSON file"""
        # Don't save during initialization
        if self.config_loading:
            return
            
        try:
            config = {
                'mac_address': self.mac_var.get().strip(),
                'temp_interval_sec': int(self.set_temp_var.get()) if self.set_temp_var.get().strip() else 0,
                'feed_interval_sec': int(self.set_feed_var.get()) if self.set_feed_var.get().strip() else 0,
                'last_feed_time': self.last_feed_time_formatted
            }
            
            with open(CONFIG_FILE, 'w') as f:
                json.dump(config, f, indent=2)
            
            print(f"Configuration saved to {CONFIG_FILE}")
        except Exception as e:
            print(f"Error saving config: {e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = IoTApp(root)
    root.mainloop()
