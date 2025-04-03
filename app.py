from flask import Flask, request, jsonify
import cv2
import numpy as np
import os
import requests
import asyncio
import threading
from telegram import Bot
import io
import time

app = Flask(__name__)

# Telegram Bot Configuration
TELEGRAM_BOT_TOKEN = "7555720722:AAHnCQW2M70jIFH1Ol08lO9UDqp2RBHqmSc"
TELEGRAM_CHAT_ID = "7081127777"  # The chat ID where notifications will be sent

# Create a bot instance
bot = Bot(token=TELEGRAM_BOT_TOKEN)

# Function to send message and image to Telegram
async def send_to_telegram(image, detections):
    try:
        # Create message with detections
        if detections:
            message = "🔍 Object Detection Results:\n"
            for i, detection in enumerate(detections, 1):
                message += f"{i}. {detection['class']}\n"
        else:
            message = "No objects detected in the image."
        
        # Draw bounding boxes on the image
        img_with_boxes = draw_boxes(image.copy(), detections)
        
        # Convert numpy array to bytes
        is_success, buffer = cv2.imencode(".jpg", img_with_boxes)
        if not is_success:
            raise Exception("Failed to encode image")
        
        img_bytes = io.BytesIO(buffer)
        
        # Send the image first
        await bot.send_photo(
            chat_id=TELEGRAM_CHAT_ID,
            photo=img_bytes,
            caption="Processed Image"
        )
        
        # Send detection results
        await bot.send_message(
            chat_id=TELEGRAM_CHAT_ID,
            text=message
        )
        
        return True
    except Exception as e:
        print(f"Error sending to Telegram: {str(e)}")
        return False

def draw_boxes(img, detections):
    height, width, _ = img.shape
    
    # Load class information
    classes = []
    with open("coco.names", "r") as f:
        classes = [line.strip() for line in f.readlines()]
    
    colors = np.random.uniform(0, 255, size=(len(classes), 3))
    
    # Draw bounding boxes based on detection results
    for detection in detections:
        class_name = detection["class"]
        
        # If we have box coordinates, use them
        if "box" in detection:
            x, y, w, h = detection["box"]
            class_idx = classes.index(class_name)
            color = colors[class_idx].tolist()
            
            cv2.rectangle(img, (x, y), (x + w, y + h), color, 2)
            cv2.putText(img, class_name, (x, y - 5), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)
    
    return img

def notify_telegram_async(image, detections):
    """Run the async function in a new thread with proper event loop handling"""
    def run_async_func():
        # Create a new event loop for this thread
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            # Run the async function in this loop
            loop.run_until_complete(send_to_telegram(image, detections))
        finally:
            # Close the loop properly when done
            loop.close()
    
    # Start as daemon thread so it doesn't block program exit
    thread = threading.Thread(target=run_async_func, daemon=True)
    thread.start()

# Load YOLO model
def load_yolo():
    net = cv2.dnn.readNet("yolov3.weights", "yolov3.cfg")
    classes = []
    with open("coco.names", "r") as f:
        classes = [line.strip() for line in f.readlines()]
    layers_names = net.getLayerNames()
    output_layers = [layers_names[i-1] for i in net.getUnconnectedOutLayers()]
    return net, classes, output_layers

# Download required files if they don't exist
def download_yolo_files():
    urls = {
        "yolov3.weights": "https://pjreddie.com/media/files/yolov3.weights",
        "yolov3.cfg": "https://raw.githubusercontent.com/pjreddie/darknet/master/cfg/yolov3.cfg",
        "coco.names": "https://raw.githubusercontent.com/pjreddie/darknet/master/data/coco.names"
    }
    
    for filename, url in urls.items():
        if not os.path.exists(filename):
            print(f"Downloading {filename}...")
            try:
                response = requests.get(url, stream=True)
                response.raise_for_status()
                
                with open(filename, 'wb') as f:
                    if filename == "yolov3.weights":
                        # For weights file, download in chunks due to large size
                        for chunk in response.iter_content(chunk_size=8192):
                            if chunk:
                                f.write(chunk)
                    else:
                        f.write(response.content)
                print(f"Successfully downloaded {filename}")
            except Exception as e:
                print(f"Error downloading {filename}: {str(e)}")
                raise

@app.route('/detect', methods=['POST'])
def detect_objects():
    try:
        # Check if image is in request
        if 'image' not in request.files:
            return jsonify({"error": "No image provided"}), 400
        
        file = request.files['image']
        if file.filename == '':
            return jsonify({"error": "No selected file"}), 400

        # Read image
        file_bytes = file.read()
        img = cv2.imdecode(np.frombuffer(file_bytes, np.uint8), cv2.IMREAD_COLOR)
        height, width, _ = img.shape

        # Load YOLO model
        net, classes, output_layers = load_yolo()

        # Detecting objects
        blob = cv2.dnn.blobFromImage(img, 0.00392, (416, 416), (0, 0, 0), True, crop=False)
        net.setInput(blob)
        outs = net.forward(output_layers)

        # Information to display on screen
        class_ids = []
        confidences = []
        boxes = []

        # Showing information on the screen
        for out in outs:
            for detection in out:
                scores = detection[5:]
                class_id = np.argmax(scores)
                confidence = scores[class_id]
                if confidence > 0.5:
                    # Object detected
                    center_x = int(detection[0] * width)
                    center_y = int(detection[1] * height)
                    w = int(detection[2] * width)
                    h = int(detection[3] * height)

                    # Rectangle coordinates
                    x = int(center_x - w / 2)
                    y = int(center_y - h / 2)

                    boxes.append([x, y, w, h])
                    confidences.append(float(confidence))
                    class_ids.append(class_id)

        # Apply non-max suppression
        indexes = cv2.dnn.NMSBoxes(boxes, confidences, 0.5, 0.4)

        # Prepare results
        results = []
        for i in range(len(boxes)):
            if i in indexes:
                x, y, w, h = boxes[i]
                label = str(classes[class_ids[i]])
                results.append({
                    "class": label,
                    "box": [x, y, w, h]  # Include box coordinates for drawing
                })
        
        # Send notification to Telegram bot
        notify_telegram_async(img, results)

        # Remove box coordinates from API response if not needed
        api_results = []
        for result in results:
            api_results.append({
                "class": result["class"]
            })

        return jsonify({"detections": api_results})

    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    # Download required files
    download_yolo_files()
    # Run the app
    port = int(os.environ.get("PORT", 5000))
    app.run(debug=False, host='0.0.0.0', port=port)