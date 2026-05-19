// Converts a hex color string (e.g. "#FF0000") to a Pebble GColor8 byte.
// GColor8 uses 2 bits per channel (0-3) with the top 2 bits set (0b11xxxxxx).
function hexToGColor8(hex) {
  var r = Math.round((parseInt(hex.substring(1,3), 16) / 255) * 3) & 3;
  var g = Math.round((parseInt(hex.substring(3,5), 16) / 255) * 3) & 3;
  var b = Math.round((parseInt(hex.substring(5,7), 16) / 255) * 3) & 3;
  return (0b11 << 6) | (r << 4) | (g << 2) | b;
}

// Converts a GColor8 ARGB byte back to a hex color string (e.g. "#C0C0C0").
// Extracts 2-bit channels and multiplies by 85 to scale to 0-255.
function gcolorToHex(argb) {
  var r = Math.round(((argb >> 4) & 3) * 85);
  var g = Math.round(((argb >> 2) & 3) * 85);
  var b = Math.round((argb & 3) * 85);
  return '#' + [r,g,b].map(function(c) {
    return ('0' + c.toString(16)).slice(-2);
  }).join('');
}

// Generates HTML for all 64 Pebble GColor8 color swatches.
// Each swatch stores its GColor8 value and a "xor" preview color (inverted) for the selection marker.
var COLOR_SWATCHES = (function() {
  var h = '';
  for (var ci = 0; ci < 64; ci++) {
    var r = ((ci >> 4) & 3) * 85;
    var g = ((ci >> 2) & 3) * 85;
    var b = (ci & 3) * 85;
    var hex = '#' + [r,g,b].map(function(c) { return ('0' + c.toString(16)).slice(-2); }).join('');
    var val = 0xC0 | ci;
    h += '<div class="color-swatch" style="background:' + hex + '" data-val="' + val + '" data-xor="' + gcolorToHex(val ^ 0x3F) + '" onclick="pickColor(this)"></div>';
  }
  return h;
})();

// Full HTML page for the Pebble configuration window.
// Injected with initial config values and opened via Pebble.openURL().
var CONFIG_HTML = '<!DOCTYPE html><html><head><meta charset="utf-8">' +
'<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">' +
'<title>Pebble Junkies</title>' +
'<style>' +
'*{box-sizing:border-box;margin:0;padding:0}' +
'body{font-family:-apple-system,Helvetica,sans-serif;background:#1a1a2e;color:#eee;padding:16px;max-width:400px;margin:0 auto}' +
'h2{font-size:18px;margin:20px 0 8px;border-bottom:1px solid #333;padding-bottom:4px}' +
'h2:first-child{margin-top:0}' +
'label{display:block;margin:10px 0 4px;font-size:13px;color:#aaa}' +
'select,input[type=text],input[type=number]{width:100%;padding:8px;background:#16213e;color:#eee;border:1px solid #0f3460;border-radius:6px;font-size:15px}' +
'.row{display:flex;gap:10px;margin:0}' +
'.col{flex:1}' +
'.toggle-wrap{display:flex;align-items:center;justify-content:space-between;margin:10px 0;padding:8px;background:#16213e;border-radius:6px}' +
'.toggle-wrap label{margin:0}' +
'.toggle{position:relative;width:48px;height:26px;background:#555;border-radius:13px;cursor:pointer;transition:0.2s}' +
'.toggle.on{background:#0f3460}' +
'.toggle::after{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;background:white;border-radius:50%;transition:0.2s}' +
'.toggle.on::after{left:25px}' +
'.color-grid{display:flex;flex-wrap:wrap;gap:2px;margin:4px 0 8px}' +
'.color-swatch{width:22px;height:22px;border-radius:3px;border:2px solid transparent;cursor:pointer}' +
'.color-swatch.selected{}' +
'button{width:100%;padding:12px;background:#0f3460;color:white;border:none;border-radius:8px;font-size:17px;font-weight:600;margin-top:24px;cursor:pointer}' +
'button:active{background:#1a5276}' +
'.night-section{border-left:3px solid #0f3460;padding-left:12px;margin-top:8px}' +
'.note{font-size:11px;color:#666;margin:4px 0}' +
'.about{margin-top:20px;padding:12px;background:#16213e;border-radius:8px;text-align:center}' +
'.about p{font-size:11px;color:#888;margin:2px 0}' +
'.about strong{color:#ccc}' +
'</style></head><body>' +
'<h2>Time &amp; Date</h2>' +
'<div class="toggle-wrap"><label>24-hour format</label><div id="toggle24h" class="toggle" onclick="toggle(this)"></div></div>' +
'<label>Date format</label>' +
'<select id="dateFormat">' +
'<option value="%a, %b %d">Mon, Jan 01</option>' +
'<option value="%a %d %b">Mon 01 Jan</option>' +
'<option value="%a, %d %b %Y">Mon, 01 Jan 2026</option>' +
'<option value="%b %d %a">Jan 01 Mon</option>' +
'<option value="%d/%m">01/01</option>' +
'<option value="%m/%d">01/01</option>' +
'<option value="%d/%m/%y">01/01/26</option>' +
'<option value="%a %d">Mon 01</option>' +
'<option value="%d %b">01 Jan</option>' +
'</select>' +
'<h2>Weather</h2>' +
'<label>Temperature unit</label>' +
'<select id="tempUnit"><option value="0">Celsius</option><option value="1">Fahrenheit</option><option value="2">Kelvin</option></select>' +
'<label>Refresh interval</label>' +
'<select id="weatherInterval"><option value="5">5 minutes</option><option value="15">15 minutes</option><option value="30">30 minutes</option><option value="60" selected>60 minutes</option></select>' +
'<div class="toggle-wrap"><label>Use GPS for location</label><div id="toggleGps" class="toggle" onclick="toggle(this)"></div></div>' +
'<div id="manualLoc" style="display:none">' +
'<label>City or location name</label><input type="text" id="locName" placeholder="e.g. London">' +
'<div class="note">City will be looked up automatically</div>' +
'</div>' +
'<h2>Bottom Quadrants</h2>' +
'<div class="row"><div class="col"><label>Top-Left</label><select id="quadTL"><option value="0">Off</option><option value="1">Battery</option><option value="2">Steps</option><option value="3">Heart Rate</option><option value="4">Rain %</option><option value="5">Temperature</option><option value="6">Weather</option></select></div>' +
'<div class="col"><label>Top-Right</label><select id="quadTR"><option value="0">Off</option><option value="1">Battery</option><option value="2">Steps</option><option value="3">Heart Rate</option><option value="4">Rain %</option><option value="5">Temperature</option><option value="6">Weather</option></select></div></div>' +
'<div class="row"><div class="col"><label>Bottom-Left</label><select id="quadBL"><option value="0">Off</option><option value="1">Battery</option><option value="2">Steps</option><option value="3">Heart Rate</option><option value="4">Rain %</option><option value="5">Temperature</option><option value="6">Weather</option></select></div>' +
'<div class="col"><label>Bottom-Right</label><select id="quadBR"><option value="0">Off</option><option value="1">Battery</option><option value="2">Steps</option><option value="3">Heart Rate</option><option value="4">Rain %</option><option value="5">Temperature</option><option value="6">Weather</option></select></div></div>' +
'<h2>Colors</h2>' +
'<label>Foreground (day)</label><div id="fgDayPicker" class="color-grid">'+COLOR_SWATCHES+'</div><input type="hidden" id="fgDay" value="255">' +
'<label>Background (day)</label><div id="bgDayPicker" class="color-grid">'+COLOR_SWATCHES+'</div><input type="hidden" id="bgDay" value="0">' +
'<div class="toggle-wrap"><label>Day/Night auto-switch</label><div id="toggleDayNight" class="toggle" onclick="toggle(this)"></div></div>' +
'<div id="nightColors" class="night-section" style="display:none">' +
'<label>Foreground (night)</label><div id="fgNightPicker" class="color-grid">'+COLOR_SWATCHES+'</div><input type="hidden" id="fgNight" value="0">' +
'<label>Background (night)</label><div id="bgNightPicker" class="color-grid">'+COLOR_SWATCHES+'</div><input type="hidden" id="bgNight" value="255">' +
'</div>' +
'<div class="about">' +
'<p><strong>Pebble Junkies v1.0</strong></p>' +
'<p>Vibe Coded by Adrian Chiang (with OpenCode)</p>' +
'<p>Code is open sourced at github.com/ajack2001my/pebble-junkies</p>' +
'</div>' +
'<button onclick="save()">Save Settings</button>' +
'<script>' +
'function pickColor(el){var p=el.parentNode;for(var i=0;i<p.children.length;i++){p.children[i].classList.remove("selected");p.children[i].style.backgroundImage=""}el.classList.add("selected");el.style.backgroundImage="radial-gradient(circle at 50% 50%,"+el.getAttribute("data-xor")+" 35%,transparent 40%)";document.getElementById(p.id.replace("Picker","")).value=el.getAttribute("data-val")}' +
'function selectColor(pid,val){var p=document.getElementById(pid);for(var i=0;i<p.children.length;i++){if(parseInt(p.children[i].getAttribute("data-val"))===val){p.children[i].classList.add("selected");p.children[i].style.backgroundImage="radial-gradient(circle at 50% 50%,"+p.children[i].getAttribute("data-xor")+" 35%,transparent 40%)"}}}' +
'function toggle(el){el.classList.toggle("on");var id=el.id;if(id==="toggleGps"){document.getElementById("manualLoc").style.display=el.classList.contains("on")?"none":"block"}' +
'if(id==="toggleDayNight"){document.getElementById("nightColors").style.display=el.classList.contains("on")?"block":"none"}}' +
'function getQueryParam(name){var match=location.search.match(new RegExp("[?&]"+name+"=([^&]*)"));return match?decodeURIComponent(match[1].replace(/\\+/g," ")):null}' +
'function load(){try{var cfg;if(typeof initialConfig!==\'undefined\'&&initialConfig){cfg=initialConfig}else{var raw=getQueryParam("config");if(!raw)return;cfg=JSON.parse(raw)}' +
'setToggle("toggle24h",cfg.use24h);' +
'setSelect("dateFormat",cfg.dateFormat);' +
'setSelect("tempUnit",cfg.tempUnit);' +
'setSelect("weatherInterval",cfg.weatherInterval);' +
'setToggle("toggleGps",cfg.useGPS);' +
'if(cfg.locationStr)setVal("locName",cfg.locationStr);' +
'setSelect("quadTL",cfg.quadTL);setSelect("quadTR",cfg.quadTR);' +
'setSelect("quadBL",cfg.quadBL);setSelect("quadBR",cfg.quadBR);' +
'if(cfg.fgDay!==undefined)selectColor("fgDayPicker",cfg.fgDay);' +
'if(cfg.bgDay!==undefined)selectColor("bgDayPicker",cfg.bgDay);' +
'setToggle("toggleDayNight",cfg.useDayNight);' +
'if(cfg.fgNight!==undefined)selectColor("fgNightPicker",cfg.fgNight);' +
'if(cfg.bgNight!==undefined)selectColor("bgNightPicker",cfg.bgNight);' +
'if(!cfg.useGPS)document.getElementById("manualLoc").style.display="block";' +
'if(cfg.useDayNight)document.getElementById("nightColors").style.display="block";if(typeof caps!==\'undefined\')filterOptions(caps)}catch(e){}}' +
'function setToggle(id,val){var el=document.getElementById(id);if(val){el.classList.add("on")}}' +
'function setSelect(id,val){if(val!==null&&val!==undefined){document.getElementById(id).value=val}}' +
'function setVal(id,val){document.getElementById(id).value=val}' +
'function filterOptions(caps){var ids=["quadTL","quadTR","quadBL","quadBR"];for(var si=0;si<ids.length;si++){var sel=document.getElementById(ids[si]);for(var i=sel.options.length-1;i>=0;i--){var v=parseInt(sel.options[i].value);if(v===2&&!(caps&1)||v===3&&!(caps&2))sel.remove(i)}}}' +
'function save(){var config={};' +
'config.use24h=document.getElementById("toggle24h").classList.contains("on")?1:0;' +
'config.dateFormat=document.getElementById("dateFormat").value;' +
'config.tempUnit=parseInt(document.getElementById("tempUnit").value);' +
'config.weatherInterval=parseInt(document.getElementById("weatherInterval").value);' +
'config.useGPS=document.getElementById("toggleGps").classList.contains("on")?1:0;' +
'if(!config.useGPS){config.locationStr=document.getElementById("locName").value||""}' +
'config.quadTL=parseInt(document.getElementById("quadTL").value);' +
'config.quadTR=parseInt(document.getElementById("quadTR").value);' +
'config.quadBL=parseInt(document.getElementById("quadBL").value);' +
'config.quadBR=parseInt(document.getElementById("quadBR").value);' +
'config.fgDay=parseInt(document.getElementById("fgDay").value);' +
'config.bgDay=parseInt(document.getElementById("bgDay").value);' +
'config.useDayNight=document.getElementById("toggleDayNight").classList.contains("on")?1:0;' +
'if(config.useDayNight){' +
'config.fgNight=parseInt(document.getElementById("fgNight").value);' +
'config.bgNight=parseInt(document.getElementById("bgNight").value)}' +
'document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify(config))}' +
'window.onload=load<\/script></body></html>';

// In-memory cache of the most recent settings received from the watchface.
// Used to pre-populate the config page so the user sees their current values.
var cachedSettings = {};

// Capability bitmask from the watch — tells us which features the hardware supports.
// Bit 0 = steps (HealthService), Bit 1 = heart rate (HR sensor).
// On Aplite the watch always sends 0, so Steps and HR are hidden from the config page.
var cachedCapabilities = 0;

// Tracks the device's current location (lat/lon) for weather API calls
var locationData = {
  lat: null,
  lon: null,
  name: ''
};

// Sends an error message (key 9) to the watchface for display
function sendError(msg) {
  var d = {}; d['9'] = msg;
  Pebble.sendAppMessage(d);
}

// Fetches weather data from Open-Meteo API (free, no key required) and sends it to the watchface.
// lat/lon are decimal degrees. Sends temperature, high/low, condition code, precipitation, sunrise/sunset.
function fetchWeatherFromAPI(lat, lon) {
  var url = 'https://api.open-meteo.com/v1/forecast?' +
    'latitude=' + lat + '&longitude=' + lon +
    '&current=temperature_2m,precipitation_probability,weather_code' +
    '&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset' +
    '&timezone=auto';

  var req = new XMLHttpRequest();
  req.timeout = 20000;
  req.open('GET', url, true);
  req.onload = function() {
    if (req.status === 200) {
      try {
        var data = JSON.parse(req.responseText);
        var current = data.current;
        var daily = data.daily;
        if (!current || !daily) { sendError('Incomplete data'); return; }

        var sunriseDate = new Date(daily.sunrise[0]);
        var sunsetDate = new Date(daily.sunset[0]);
        var sunriseMin = sunriseDate.getHours() * 60 + sunriseDate.getMinutes();
        var sunsetMin = sunsetDate.getHours() * 60 + sunsetDate.getMinutes();

        var locName = data.timezone || locationData.name || '';
        var wd = {};
        wd['1'] = Math.round(current.temperature_2m * 10);
        wd['2'] = Math.round(daily.temperature_2m_max[0] * 10);
        wd['3'] = Math.round(daily.temperature_2m_min[0] * 10);
        wd['4'] = current.weather_code;
        wd['5'] = Math.round(current.precipitation_probability || 0);
        wd['6'] = sunriseMin;
        wd['7'] = sunsetMin;
        wd['8'] = locName;
        Pebble.sendAppMessage(wd);
      } catch(e) {
        sendError('Parse error');
      }
    } else {
      sendError('HTTP ' + req.status);
    }
  };
  req.onerror = function() { sendError('Network error'); };
  req.ontimeout = function() { sendError('Timeout'); };
  req.send();
}

// Fetches weather using the stored lat/lon, or falls back to a London default
function fetchWeatherWithLocation() {
  if (locationData.lat !== null && locationData.lon !== null) {
    fetchWeatherFromAPI(locationData.lat, locationData.lon);
    return;
  }
  locationData.lat = 51.5;
  locationData.lon = -0.12;
  locationData.name = 'London';
  fetchWeatherFromAPI(locationData.lat, locationData.lon);
}

// Requests the device's GPS position, then fetches weather. Falls back to default on failure.
function requestLocationAndFetch() {
  navigator.geolocation.getCurrentPosition(
    function(pos) {
      locationData.lat = pos.coords.latitude;
      locationData.lon = pos.coords.longitude;
      fetchWeatherFromAPI(locationData.lat, locationData.lon);
    },
    function(err) {
      fetchWeatherWithLocation();
    },
    { timeout: 15000, enableHighAccuracy: false }
  );
}

// Geocodes a city name to lat/lon using the Open-Meteo Geocoding API, then calls the callback.
function geocodeCity(city, callback) {
  var url = 'https://geocoding-api.open-meteo.com/v1/search?name=' + encodeURIComponent(city) + '&count=1&language=en&format=json';
  var req = new XMLHttpRequest();
  req.timeout = 15000;
  req.open('GET', url, true);
  req.onload = function() {
    if (req.status === 200) {
      try {
        var data = JSON.parse(req.responseText);
        if (data.results && data.results.length > 0) {
          callback(data.results[0].latitude, data.results[0].longitude, data.results[0].name || city);
        } else {
          sendError('City not found: ' + city);
        }
      } catch(e) { sendError('Geocode parse error'); }
    } else { sendError('Geocode HTTP ' + req.status); }
  };
  req.onerror = function() { sendError('Geocode network error'); };
  req.ontimeout = function() { sendError('Geocode timeout'); };
  req.send();
}

// Opens the configuration page when the user requests it from the Pebble mobile app.
// If cached settings exist, pre-populates the page. Otherwise requests settings from the watchface
// and waits up to 3 seconds before falling back to defaults.
Pebble.addEventListener('showConfiguration', function(e) {
  if (Object.keys(cachedSettings).length > 0) {
    var configJson = JSON.stringify(cachedSettings);
    var html = CONFIG_HTML.replace('window.onload=load', 'var initialConfig=' + configJson + ';var caps=' + cachedCapabilities + ';window.onload=load');
    Pebble.openURL('data:text/html,' + encodeURIComponent(html));
  } else {
    var d = {}; d['28'] = 1;
    Pebble.sendAppMessage(d);
    var poll = setInterval(function() {
      if (Object.keys(cachedSettings).length > 0) {
        clearInterval(poll); clearTimeout(fallback);
        var configJson = JSON.stringify(cachedSettings);
        var html = CONFIG_HTML.replace('window.onload=load', 'var initialConfig=' + configJson + ';var caps=' + cachedCapabilities + ';window.onload=load');
        Pebble.openURL('data:text/html,' + encodeURIComponent(html));
      }
    }, 50);
    var fallback = setTimeout(function() {
      clearInterval(poll);
      Pebble.openURL('data:text/html,' + encodeURIComponent(CONFIG_HTML));
    }, 3000);
  }
});

// Encodes the settings object into a packed byte array matching the C-side Settings struct.
// Order of fields must match the struct definition exactly.
function encodeSettings(config) {
  var buf = [];
  buf.push(3); // version
  buf.push(config.use24h ? 1 : 0);
  var df = config.dateFormat || '';
  for (var i = 0; i < 20; i++) buf.push(i < df.length ? df.charCodeAt(i) : 0);
  buf.push(config.tempUnit || 0);
  buf.push(config.weatherInterval || 60);
  buf.push(config.quadTL || 0);
  buf.push(config.quadTR || 0);
  buf.push(config.quadBL || 0);
  buf.push(config.quadBR || 0);
  buf.push(config.fgDay || 0);
  buf.push(config.bgDay || 0);
  buf.push(config.useDayNight ? 1 : 0);
  buf.push(config.fgNight || 0);
  buf.push(config.bgNight || 0);
  buf.push(config.useGPS ? 1 : 0);
  var lat = config.latitude || 0;
  buf.push(lat & 0xFF); buf.push((lat >> 8) & 0xFF);
  buf.push((lat >> 16) & 0xFF); buf.push((lat >> 24) & 0xFF);
  var lon = config.longitude || 0;
  buf.push(lon & 0xFF); buf.push((lon >> 8) & 0xFF);
  buf.push((lon >> 16) & 0xFF); buf.push((lon >> 24) & 0xFF);
  var ln = config.locationStr || '';
  for (var i = 0; i < 40; i++) buf.push(i < ln.length ? ln.charCodeAt(i) : 0);
  return buf;
}

// Decodes a packed byte array (from the C-side Settings blob) back into a JS settings object.
// Inverse of encodeSettings — must agree on field order and sizes.
function decodeSettings(buf) {
  var cfg = {}, pos = 0;
  cfg.version = buf[pos++];
  cfg.use24h = buf[pos++];
  var df = '';
  for (var i = 0; i < 20; i++) { var c = buf[pos++]; if (c) df += String.fromCharCode(c); }
  cfg.dateFormat = df;
  cfg.tempUnit = buf[pos++];
  cfg.weatherInterval = buf[pos++];
  cfg.quadTL = buf[pos++];
  cfg.quadTR = buf[pos++];
  cfg.quadBL = buf[pos++];
  cfg.quadBR = buf[pos++];
  cfg.fgDay = buf[pos++];
  cfg.bgDay = buf[pos++];
  cfg.useDayNight = buf[pos++];
  cfg.fgNight = buf[pos++];
  cfg.bgNight = buf[pos++];
  cfg.useGPS = buf[pos++];
  cfg.latitude = buf[pos] | (buf[pos+1] << 8) | (buf[pos+2] << 16) | (buf[pos+3] << 24);
  pos += 4;
  cfg.longitude = buf[pos] | (buf[pos+1] << 8) | (buf[pos+2] << 16) | (buf[pos+3] << 24);
  pos += 4;
  var ln = '';
  for (var i = 0; i < 40; i++) { var c = buf[pos++]; if (c) ln += String.fromCharCode(c); }
  cfg.locationStr = ln;
  return cfg;
}

// Called when the configuration page is closed. Parses the returned JSON config,
// Called when the configuration page is closed. Parses the returned JSON config,
// caches it, sends quadrant and settings blob to the watchface, and triggers weather fetch.
Pebble.addEventListener('webviewclosed', function(e) {
  var response = e.response;
  if (response && response.length > 0) {
    var config;
    try {
      config = JSON.parse(response);
    } catch(_e) {
      try {
        config = JSON.parse(decodeURIComponent(response));
      } catch(_e2) {
        return;
      }
    }
    for (var k in config) {
      cachedSettings[k] = config[k];
    }
    setTimeout(function() {
      var qd = {};
      qd['13'] = config.quadTL || 0;
      qd['14'] = config.quadTR || 0;
      qd['15'] = config.quadBL || 0;
      qd['16'] = config.quadBR || 0;
      qd['26'] = encodeSettings(config);
      Pebble.sendAppMessage(qd);
    }, 2000);
    if (config.useGPS) {
      requestLocationAndFetch();
    } else if (config.locationStr) {
      geocodeCity(config.locationStr, function(lat, lon, name) {
        locationData.lat = lat;
        locationData.lon = lon;
        locationData.name = name;
        cachedSettings.latitude = Math.round(lat * 10000);
        cachedSettings.longitude = Math.round(lon * 10000);
        fetchWeatherFromAPI(lat, lon);
      });
    }
  }
});

// Handles incoming messages from the watchface: parses settings blobs, triggers weather,
// and responds to settings requests.
Pebble.addEventListener('appmessage', function(e) {
  if (e.payload) {
    // Receive and decode full settings blob from the watchface
    if (e.payload.settingsBlob !== undefined) {
      var raw = e.payload.settingsBlob;
      var buf = [];
      if (raw.byteLength !== undefined) {
        var view = new Uint8Array(raw);
        for (var i = 0; i < view.length; i++) buf.push(view[i]);
      } else {
        for (var i = 0; i < raw.length; i++) buf.push(raw[i]);
      }
      var cfg = decodeSettings(buf);
      for (var k in cfg) {
        cachedSettings[k] = cfg[k];
      }
    }
    // Capture platform capabilities bitmask (KEY_CAPABILITIES = 29) sent by the watchface
    if (e.payload['29'] !== undefined) {
      cachedCapabilities = e.payload['29'];
    }
    // Weather fetch requested by the watchface tick
    if (e.payload.requestWeather !== undefined) {
      if (locationData.lat !== null && locationData.lon !== null) {
        fetchWeatherFromAPI(locationData.lat, locationData.lon);
      } else {
        requestLocationAndFetch();
      }
    }
    // Watchface requesting current settings (e.g., after boot or config open)
    if (e.payload.requestSettings !== undefined) {
      if (Object.keys(cachedSettings).length > 0) {
        var qd = {};
        qd['13'] = cachedSettings.quadTL || 0;
        qd['14'] = cachedSettings.quadTR || 0;
        qd['15'] = cachedSettings.quadBL || 0;
        qd['16'] = cachedSettings.quadBR || 0;
        Pebble.sendAppMessage(qd);
      }
    }
  }
});
