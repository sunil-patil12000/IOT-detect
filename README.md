# Object Detection API

This is a Flask-based REST API that performs object detection using YOLOv3. The API can detect 80 different types of objects in images.

## Setup

1. Install the required dependencies:
```bash
pip install -r requirements.txt
```

2. Run the Flask application:
```bash
python app.py
```

The application will automatically download the required YOLOv3 model files on first run.

## API Usage

### Endpoint: POST /detect

Detects objects in an uploaded image.

**Request:**
- Method: POST
- URL: `http://localhost:5000/detect`
- Content-Type: multipart/form-data
- Body: 
  - `image`: Image file to process

**Response:**
```json
{
    "detections": [
        {
            "class": "object_name",
            "confidence": 0.95,
            "box": {
                "x": 100,
                "y": 200,
                "width": 300,
                "height": 400
            }
        }
    ]
}
```

### Example using curl:
```bash
curl -X POST -F "image=@path/to/your/image.jpg" http://localhost:5000/detect
```

## Supported Object Classes

The API can detect 80 different types of objects including:
- Person
- Car
- Dog
- Cat
- Chair
- Bottle
- And many more...

For a complete list of supported objects, check the `coco.names` file that will be downloaded automatically.

## Deployment on Render

This application can be easily deployed to Render:

1. Create a new account on [Render](https://render.com) if you don't have one
2. Fork this repository to your GitHub account
3. In the Render dashboard, click "New" and select "Blueprint"
4. Connect your GitHub account and select your forked repository
5. Click "Apply" and Render will automatically deploy your application using the configuration in `render.yaml`

Alternatively, for manual deployment:

1. Go to the Render dashboard and click "New" > "Web Service"
2. Connect your repository
3. Use the following settings:
   - Environment: Python
   - Build Command: `pip install -r requirements.txt`
   - Start Command: `gunicorn app:app`
4. Click "Create Web Service"

Once deployed, you can access your API at the URL provided by Render.