require('dotenv').config();

console.log('Google Maps API Key:', process.env.MAPS_API);


const express = require('express');
const bodyParser = require('body-parser');
const http = require('http');
const socketIo = require('socket.io');
const path = require('path');
const nodemailer = require('nodemailer');

const app = express();
const server = http.createServer(app);
const io = socketIo(server);

app.use(bodyParser.json());
app.use(bodyParser.urlencoded({ extended: true }));
app.use(express.static(path.join(__dirname, 'public')));

let latestData = {
  timestamp: new Date().toISOString(),
  values: {},
  raw: "No data received yet"
};

const movementHistory = [];
const MOVEMENT_THRESHOLD = 3;
let lastAlertTime = 0;
const ALERT_COOLDOWN = 5 * 60 * 1000; 

const emailTransporter = nodemailer.createTransport({
  service: 'gmail', 
  auth: {
    user: 'ralchev.nikola@gmail.com',
    pass: process.env.EMAIL_PASSWORD
  }
});

const emailConfig = {
  to: 'ralchev.nikola@gmail.com', 
  subject: 'GPS Tracker Movement Alert',
  html: ''
};

function sendEmailAlert(deviceData) {
  const currentTime = new Date().getTime();
  
  if (currentTime - lastAlertTime < ALERT_COOLDOWN) {
    console.log('Alert cooldown active, skipping email');
    return;
  }
  
  lastAlertTime = currentTime;
  
  let mapLink = '';
  if (deviceData.lat && deviceData.lon) {
    mapLink = `https://maps.google.com/?q=${deviceData.lat},${deviceData.lon}`;
  }
  
  emailConfig.html = `
    <h2>Movement Alert from GPS Tracker</h2>
    <p>Your GPS tracker has detected continuous movement.</p>
    <p><strong>Device ID:</strong> ${deviceData.deviceId || 'Unknown'}</p>
    <p><strong>Timestamp:</strong> ${new Date().toLocaleString()}</p>
    <p><strong>Location:</strong> ${mapLink ? `<a href="${mapLink}">View on Google Maps</a>` : 'Location data not available'}</p>
  `;
  
  emailTransporter.sendMail(emailConfig, (error, info) => {
    if (error) {
      console.error('Error sending email alert:', error);
    } else {
      console.log('Email alert sent:', info.response);
    }
  });
}

app.post('/api/data', (req, res) => {
  console.log('Received data:', req.body);
  
  latestData = {
    timestamp: new Date().toISOString(),
    values: req.body,
    raw: JSON.stringify(req.body)
  };
  
  if (req.body.isMoving !== undefined) {
    const isMoving = req.body.isMoving;
    
    movementHistory.push(isMoving);
    
    if (movementHistory.length > MOVEMENT_THRESHOLD) {
      movementHistory.shift();
    }
    
    if (movementHistory.length === MOVEMENT_THRESHOLD && 
        movementHistory.every(status => status === true)) {
      console.log('Continuous movement detected, sending alert');
      sendEmailAlert(req.body);
    }
  }
  
  io.emit('newData', latestData);
  
  res.status(200).json({ status: 'success', message: 'Data received' });
});

app.get('/api/data', (req, res) => {
  res.json(latestData);
});

app.get('/api/movement', (req, res) => {
  res.json({
    history: movementHistory,
    consecutiveCount: movementHistory.filter(status => status === true).length
  });
});

app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

io.on('connection', (socket) => {
  console.log('New client connected');
  
  socket.emit('newData', latestData);
  
  socket.on('disconnect', () => {
    console.log('Client disconnected');
  });
});

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
  console.log(`Server is running on port ${PORT}`);
});


app.get('/api/config', (req, res) => {
    res.json({ googleApiKey: process.env.MAPS_API });
  });
  