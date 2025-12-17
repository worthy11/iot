import tkinter as tk
from tkinter import ttk, messagebox
import socket
import paho.mqtt.client as mqtt
import threading

# Configuration
BROKER = "10.177.164.196"
PORT = 1883
USER_ID = "f8e87394"
TOPIC_SUBSCRIPTION = f"{USER_ID}/+/data/#"
DEFAULT_MAC = "6C:C8:40:5C:63:E8"

class IoTApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Aquatest Controller")
        self.root.geometry("450x750")  # Adjusted height for combined UI
        
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

        monitor_frame = ttk.LabelFrame(main_frame, text="Live Monitor", padding="10")
        monitor_frame.pack(fill=tk.X, pady=5)

        self.temp_var = tk.StringVar(value="--")
        self.create_card(monitor_frame, "TEMPERATURE", self.temp_var, "°C")

        self.ph_var = tk.StringVar(value="--")
        self.create_card(monitor_frame, "PH LEVEL", self.ph_var, "")

        self.feed_var = tk.StringVar(value="--:--:--")
        self.create_card(monitor_frame, "LAST FEED", self.feed_var, "")

        control_frame = ttk.LabelFrame(main_frame, text="Control Panel", padding="10")
        control_frame.pack(fill=tk.BOTH, expand=True, pady=10)

        mac_frame = ttk.Frame(control_frame)
        mac_frame.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(mac_frame, text="Target MAC:", width=12).pack(side=tk.LEFT)
        self.mac_var = tk.StringVar(value=DEFAULT_MAC)
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
        
        # Start MQTT in a separate thread
        self.mqtt_thread = threading.Thread(target=self.start_mqtt, daemon=True)
        self.mqtt_thread.start()

    def create_card(self, parent, title, value_var, unit):
        card = ttk.Frame(parent)
        card.pack(fill=tk.X, pady=2)
        
        row = ttk.Frame(card)
        row.pack(fill=tk.X)
        
        ttk.Label(row, text=title, style="Card.TLabel", width=15).pack(side=tk.LEFT, anchor=tk.W)
        ttk.Label(row, textvariable=value_var, style="Value.TLabel").pack(side=tk.LEFT)
        if unit:
            ttk.Label(row, text=unit, font=("Segoe UI", 12), padding=(2, 5, 0, 0)).pack(side=tk.LEFT)

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
                        value, _ = payload.split(',', 1)
                        self.root.after(0, lambda v=value: self.temp_var.set(f"{float(v):.2f}"))
                    except:
                        self.root.after(0, lambda: self.temp_var.set(payload))
                elif data_type == 'ph':
                    # Format: "value,timestamp"
                    try:
                        value, _ = payload.split(',', 1)
                        self.root.after(0, lambda v=value: self.ph_var.set(f"{float(v):.2f}"))
                    except:
                        self.root.after(0, lambda: self.ph_var.set(payload))
                elif data_type == 'feed':
                    # Format: "timestamp,HH:MM:SS,status"
                    try:
                        parts_feed = payload.split(',')
                        if len(parts_feed) >= 2:
                            time_str = parts_feed[1]  # HH:MM:SS
                            self.root.after(0, lambda t=time_str: self.feed_var.set(t))
                        else:
                            self.root.after(0, lambda: self.feed_var.set(payload))
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
                    self.publish_command(f"set feed {interval}")
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

if __name__ == "__main__":
    root = tk.Tk()
    app = IoTApp(root)
    root.mainloop()
