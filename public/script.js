const connectionStatus = document.getElementById('connectionStatus');
const timestamp = document.getElementById('timestamp');
const dataGrid = document.getElementById('dataGrid');
const rawData = document.getElementById('rawData');
const latitudeDisplay = document.getElementById('latitude');
const longitudeDisplay = document.getElementById('longitude');
const pathPointsDisplay = document.getElementById('pathPoints');
const clearPathButton = document.getElementById('clearPath');

let map;
let marker;
let path;
let pathCoordinates = [];

const socket = io();

clearPathButton.addEventListener('click', () => {
  pathCoordinates = [];
  path.setPath(pathCoordinates);
  pathPointsDisplay.textContent = '0 points in path';
});

socket.on('connect', () => {
  connectionStatus.textContent = 'Connected to server';
  connectionStatus.className = 'status online';
});

socket.on('disconnect', () => {
  connectionStatus.textContent = 'Disconnected from server';
  connectionStatus.className = 'status offline';
});

socket.on('newData', (data) => {
  updateUI(data);
});

function initMap() {
  const defaultLocation = { lat: 42.6977, lng: 23.3242 };

  map = new google.maps.Map(document.getElementById('map'), {
    zoom: 15,
    center: defaultLocation,
    mapTypeId: 'roadmap',
    mapTypeControl: true,
    streetViewControl: false,
    fullscreenControl: true
  });

  marker = new google.maps.Marker({
    position: defaultLocation,
    map: map,
    title: 'ESP32 Position',
    animation: google.maps.Animation.DROP
  });

  path = new google.maps.Polyline({
    path: pathCoordinates,
    geodesic: true,
    strokeColor: '#FF0000',
    strokeOpacity: 1.0,
    strokeWeight: 2
  });

  path.setMap(map);

  fetchInitialData();
}

function updateMap(lat, lng) {
  if (!map || !marker || !path) return;

  const position = { lat: parseFloat(lat), lng: parseFloat(lng) };

  marker.setPosition(position);
  map.setCenter(position);

  pathCoordinates.push(position);
  path.setPath(pathCoordinates);

  pathPointsDisplay.textContent = `${pathCoordinates.length} points in path`;
}

function updateUI(data) {
  timestamp.textContent = `Last updated: ${new Date(data.timestamp).toLocaleString()}`;

  dataGrid.innerHTML = '';

  if (Object.keys(data.values).length === 0) {
    dataGrid.innerHTML = `
      <div class="data-item">
        <div class="data-label">Waiting for data...</div>
      </div>
    `;
  } else {
    for (const [key, value] of Object.entries(data.values)) {
      const dataItem = document.createElement('div');
      dataItem.className = 'data-item';

      const dataLabel = document.createElement('div');
      dataLabel.className = 'data-label';
      dataLabel.textContent = key;

      const dataValue = document.createElement('div');
      dataValue.className = 'data-value';
      dataValue.textContent = value;

      dataItem.appendChild(dataLabel);
      dataItem.appendChild(dataValue);
      dataGrid.appendChild(dataItem);

      if (key === 'lat') {
        latitudeDisplay.textContent = typeof value === 'number' ? value.toFixed(6) : value;
      }
      if (key === 'lon') {
        longitudeDisplay.textContent = typeof value === 'number' ? value.toFixed(6) : value;
      }
    }

    if (data.values.lat !== undefined && data.values.lon !== undefined) {
      updateMap(data.values.lat, data.values.lon);
    }
  }

  rawData.textContent = data.raw;
}

function fetchInitialData() {
  fetch('/api/data')
    .then(response => response.json())
    .then(data => {
      if (data.timestamp) {
        updateUI(data);
      }
    })
    .catch(error => {
      console.error('Error fetching initial data:', error);
    });
}

fetch('/api/config')
  .then(response => response.json())
  .then(config => {
    const googleApiKey = config.googleApiKey;
    const script = document.createElement('script');
    script.src = `https://maps.googleapis.com/maps/api/js?key=${googleApiKey}&callback=initMap`;
    script.async = true;
    script.defer = true;
    document.body.appendChild(script);
  })
  .catch(error => {
    console.error('Error fetching config:', error);
  });
