var Clay = require("pebble-clay");
var clayConfig = require("./config");
var clay = new Clay(clayConfig);

var REQUEST_WEATHER_KEY = "REQUEST_WEATHER";
var TEMPERATURE_KEY = "TEMPERATURE";
var CONDITIONS_KEY = "CONDITIONS";

var WEATHER_RANGES = [
  [0, 0, "Clear"],
  [1, 3, "Cloudy"],
  [4, 49, "Fog"],
  [50, 69, "Rain"],
  [70, 79, "Snow"],
  [80, 99, "Storm"]
];

function weatherCondition(code) {
  var label = "?";
  for (var i = 0; i < WEATHER_RANGES.length; i++) {
    var r = WEATHER_RANGES[i];
    if (code >= r[0] && code <= r[1]) label = r[2];
  }
  return label;
}

function fetchWeather(latitude, longitude) {
  var url = "https://api.open-meteo.com/v1/forecast"
    + "?latitude=" + latitude
    + "&longitude=" + longitude
    + "&current=temperature_2m,weather_code"
    + "&temperature_unit=fahrenheit";

  var xhr = new XMLHttpRequest();
  xhr.open("GET", url, true);
  xhr.onload = function () {
    try {
      var json = JSON.parse(xhr.responseText);
      var temp = Math.round(json.current.temperature_2m);
      var conditions = weatherCondition(json.current.weather_code);
      var payload = {};
      payload[TEMPERATURE_KEY] = temp;
      payload[CONDITIONS_KEY] = conditions;
      Pebble.sendAppMessage(
        payload,
        function () { console.log("weather sent: " + temp + " " + conditions); },
        function (e) { console.log("weather send failed: " + JSON.stringify(e)); }
      );
    } catch (e) {
      console.log("weather parse failed: " + e);
    }
  };
  xhr.onerror = function () { console.log("weather xhr error"); };
  xhr.send();
}

function locationSuccess(pos) {
  fetchWeather(pos.coords.latitude, pos.coords.longitude);
}

function locationError(err) {
  console.log("location error: " + err.message);
}

function refreshWeather() {
  navigator.geolocation.getCurrentPosition(locationSuccess, locationError, {
    timeout: 15000,
    maximumAge: 60000
  });
}

Pebble.addEventListener("ready", function () {
  console.log("pkjs ready");
  refreshWeather();
});

Pebble.addEventListener("appmessage", function (e) {
  if (e.payload && REQUEST_WEATHER_KEY in e.payload) {
    refreshWeather();
  }
});

