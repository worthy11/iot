import tkinter as tk
from tkinter import ttk
import socket
import paho.mqtt.client as mqtt
import threading

# Configuration
BROKER = "10.88.236.219"
PORT = 1883
USER_ID = "f8e87394"
TOPIC_SUBSCRIPTION = f"{USER_ID}/+/data/#"

class IoTSubscriberApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Aquatest")
        self.root.geometry("400x500")
        
        # Style
        style = ttk.Style()
        style.configure("TLabel", font=("Segoe UI", 12))
        style.configure("Header.TLabel", font=("Segoe UI", 14, "bold"))
        style.configure("Card.TLabel", font=("Segoe UI", 10, "bold"), foreground="#666666")
        style.configure("Value.TLabel", font=("Segoe UI", 36))
        
        # Main container with padding
        main_frame = ttk.Frame(root, padding="20")
        main_frame.pack(fill=tk.BOTH, expand=True)

        # Header with IP
        self.local_ip = self.get_local_ip()
        header_frame = ttk.Frame(main_frame)
        header_frame.pack(fill=tk.X, pady=(0, 20))
        
        ttk.Label(header_frame, text="Aquatest", style="Header.TLabel").pack(side=tk.LEFT)
        ttk.Label(header_frame, text=f"IP: {self.local_ip}", foreground="blue").pack(side=tk.RIGHT)

        # Dashboard Cards
        self.temp_var = tk.StringVar(value="--")
        self.create_card(main_frame, "TEMPERATURE", self.temp_var, "°C")

        self.ph_var = tk.StringVar(value="--")
        self.create_card(main_frame, "PH LEVEL", self.ph_var, "")

        self.feed_var = tk.StringVar(value="--:--:--")
        self.create_card(main_frame, "LAST FEED", self.feed_var, "")
        
        # Status Bar
        self.status_var = tk.StringVar(value="Connecting to MQTT...")
        status_bar = ttk.Label(root, textvariable=self.status_var, relief=tk.SUNKEN, anchor=tk.W, font=("Segoe UI", 9))
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)
        
        # MQTT Client setup
        self.client = mqtt.Client() 
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        
        # Start MQTT in a separate thread
        self.mqtt_thread = threading.Thread(target=self.start_mqtt, daemon=True)
        self.mqtt_thread.start()

    def create_card(self, parent, title, value_var, unit):
        card = ttk.Frame(parent)
        card.pack(fill=tk.X, pady=10)
        
        ttk.Label(card, text=title, style="Card.TLabel").pack(anchor=tk.W)
        
        value_frame = ttk.Frame(card)
        value_frame.pack(anchor=tk.W)
        
        ttk.Label(value_frame, textvariable=value_var, style="Value.TLabel").pack(side=tk.LEFT)
        if unit:
            ttk.Label(value_frame, text=unit, font=("Segoe UI", 14), padding=(5, 15, 0, 0)).pack(side=tk.LEFT)
            
        ttk.Separator(parent, orient="horizontal").pack(fill=tk.X, pady=5)

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
            self.status_var.set(f"Error: {str(e)}")

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
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
                
                # Update UI in main thread
                if data_type == 'temperature':
                    self.root.after(0, lambda: self.temp_var.set(payload))
                elif data_type == 'ph':
                    self.root.after(0, lambda: self.ph_var.set(payload))
                elif data_type == 'feed':
                    self.root.after(0, lambda: self.feed_var.set(payload))
                    
        except Exception as e:
            print(f"Error parsing message: {e}")

    def update_status(self, text):
        self.root.after(0, lambda: self.status_var.set(text))

if __name__ == "__main__":
    root = tk.Tk()
    app = IoTSubscriberApp(root)
    root.mainloop()
