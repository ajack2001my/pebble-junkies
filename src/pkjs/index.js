function hexToGColor8(hex) {
  var r = Math.round((parseInt(hex.substring(1,3), 16) / 255) * 3) & 3;
  var g = Math.round((parseInt(hex.substring(3,5), 16) / 255) * 3) & 3;
  var b = Math.round((parseInt(hex.substring(5,7), 16) / 255) * 3) & 3;
  return (0b11 << 6) | (r << 4) | (g << 2) | b;
}

function gcolorToHex(argb) {
  var r = Math.round(((argb >> 4) & 3) * 85);
  var g = Math.round(((argb >> 2) & 3) * 85);
  var b = Math.round((argb & 3) * 85);
  return '#' + [r,g,b].map(function(c) {
    return ('0' + c.toString(16)).slice(-2);
  }).join('');
}

var CONFIG_HTML = '<!DOCTYPE html><html><head><meta charset="utf-8">' +
'<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">' +
'<title>Watchface Settings</title>' +
'<style>' +
'*{box-sizing:border-box;margin:0;padding:0}' +
'body{font-family:-apple-system,Helvetica,sans-serif;background:#1a1a2e;color:#eee;padding:16px;max-width:400px;margin:0 auto}' +
'h2{font-size:18px;margin:20px 0 8px;border-bottom:1px solid #333;padding-bottom:4px}' +
'h2:first-child{margin-top:0}' +
'label{display:block;margin:10px 0 4px;font-size:13px;color:#aaa}' +
'select,input[type=text],input[type=number]{width:100%;padding:8px;background:#16213e;color:#eee;border:1px solid #0f3460;border-radius:6px;font-size:15px}' +
'input[type=color]{width:100%;height:40px;padding:2px;background:#16213e;border:1px solid #0f3460;border-radius:6px;cursor:pointer}' +
'.row{display:flex;gap:10px;margin:0}' +
'.col{flex:1}' +
'.toggle-wrap{display:flex;align-items:center;justify-content:space-between;margin:10px 0;padding:8px;background:#16213e;border-radius:6px}' +
'.toggle-wrap label{margin:0}' +
'.toggle{position:relative;width:48px;height:26px;background:#555;border-radius:13px;cursor:pointer;transition:0.2s}' +
'.toggle.on{background:#0f3460}' +
'.toggle::after{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;background:white;border-radius:50%;transition:0.2s}' +
'.toggle.on::after{left:25px}' +
'button{width:100%;padding:12px;background:#0f3460;color:white;border:none;border-radius:8px;font-size:17px;font-weight:600;margin-top:24px;cursor:pointer}' +
'button:active{background:#1a5276}' +
'.night-section{border-left:3px solid #0f3460;padding-left:12px;margin-top:8px}' +
'.note{font-size:11px;color:#666;margin:4px 0}' +
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
'<div class="toggle-wrap"><label>Use GPS for location</label><div id="toggleGps" class="toggle" onclick="toggle(this)"></div></div>' +
'<div id="manualLoc" style="display:none">' +
'<label>Location name (optional)</label><input type="text" id="locName" placeholder="e.g. London">' +
'<div class="row"><div class="col"><label>Latitude</label><input type="number" id="latitude" step="0.0001" placeholder="51.5"></div>' +
'<div class="col"><label>Longitude</label><input type="number" id="longitude" step="0.0001" placeholder="-0.12"></div></div>' +
'</div>' +
'<h2>Bottom Quadrants</h2>' +
'<div class="row"><div class="col"><label>Top-Left</label><select id="quadTL"><option value="0">Off</option><option value="1">Battery</option><option value="2">Steps</option><option value="3">Heart Rate</option><option value="4">Rain %</option><option value="5">Temperature</option><option value="6">Weather</option></select></div>' +
'<div class="col"><label>Top-Right</label><select id="quadTR"><option value="0">Off</option><option value="1">Battery</option><option value="2">Steps</option><option value="3">Heart Rate</option><option value="4">Rain %</option><option value="5">Temperature</option><option value="6">Weather</option></select></div></div>' +
'<div class="row"><div class="col"><label>Bottom-Left</label><select id="quadBL"><option value="0">Off</option><option value="1">Battery</option><option value="2">Steps</option><option value="3">Heart Rate</option><option value="4">Rain %</option><option value="5">Temperature</option><option value="6">Weather</option></select></div>' +
'<div class="col"><label>Bottom-Right</label><select id="quadBR"><option value="0">Off</option><option value="1">Battery</option><option value="2">Steps</option><option value="3">Heart Rate</option><option value="4">Rain %</option><option value="5">Temperature</option><option value="6">Weather</option></select></div></div>' +
'<h2>Colors</h2>' +
'<div class="row"><div class="col"><label>Foreground (day)</label><input type="color" id="fgDay" value="#ffffff"></div>' +
'<div class="col"><label>Background (day)</label><input type="color" id="bgDay" value="#000000"></div></div>' +
'<div class="toggle-wrap"><label>Day/Night auto-switch</label><div id="toggleDayNight" class="toggle" onclick="toggle(this)"></div></div>' +
'<div id="nightColors" class="night-section" style="display:none">' +
'<div class="row"><div class="col"><label>Foreground (night)</label><input type="color" id="fgNight" value="#000000"></div>' +
'<div class="col"><label>Background (night)</label><input type="color" id="bgNight" value="#ffffff"></div></div>' +
'</div>' +
'<button onclick="save()">Save Settings</button>' +
'<script>' +
'function hexToGColor8(hex){var r=Math.round((parseInt(hex.substring(1,3),16)/255)*3)&3;var g=Math.round((parseInt(hex.substring(3,5),16)/255)*3)&3;var b=Math.round((parseInt(hex.substring(5,7),16)/255)*3)&3;return(0b11<<6)|(r<<4)|(g<<2)|b}' +
'function gcolorToHex(argb){var r=Math.round(((argb>>4)&3)*85);var g=Math.round(((argb>>2)&3)*85);var b=Math.round((argb&3)*85);return"#"+[r,g,b].map(function(c){return("0"+c.toString(16)).slice(-2)}).join("")}' +
'function toggle(el){el.classList.toggle("on");var id=el.id;if(id==="toggleGps"){document.getElementById("manualLoc").style.display=el.classList.contains("on")?"none":"block"}' +
'if(id==="toggleDayNight"){document.getElementById("nightColors").style.display=el.classList.contains("on")?"block":"none"}}' +
'function getQueryParam(name){var match=location.search.match(new RegExp("[?&]"+name+"=([^&]*)"));return match?decodeURIComponent(match[1].replace(/\\+/g," ")):null}' +
'function load(){try{var cfg;if(typeof initialConfig!==\'undefined\'&&initialConfig){cfg=initialConfig}else{var raw=getQueryParam("config");if(!raw)return;cfg=JSON.parse(raw)}' +
'setToggle("toggle24h",cfg.use24h);' +
'setSelect("dateFormat",cfg.dateFormat);' +
'setSelect("tempUnit",cfg.tempUnit);' +
'setToggle("toggleGps",cfg.useGPS);' +
'if(cfg.latitude)setVal("latitude",cfg.latitude/10000);' +
'if(cfg.longitude)setVal("longitude",cfg.longitude/10000);' +
'if(cfg.locationStr)setVal("locName",cfg.locationStr);' +
'setSelect("quadTL",cfg.quadTL);setSelect("quadTR",cfg.quadTR);' +
'setSelect("quadBL",cfg.quadBL);setSelect("quadBR",cfg.quadBR);' +
'if(cfg.fgDay){var fg=Number(cfg.fgDay);if(!isNaN(fg)){setVal("fgDay",gcolorToHex(fg))}}' +
'if(cfg.bgDay){var bg=Number(cfg.bgDay);if(!isNaN(bg)){setVal("bgDay",gcolorToHex(bg))}}' +
'setToggle("toggleDayNight",cfg.useDayNight);' +
'if(cfg.fgNight){var fn=Number(cfg.fgNight);if(!isNaN(fn)){setVal("fgNight",gcolorToHex(fn))}}' +
'if(cfg.bgNight){var bn=Number(cfg.bgNight);if(!isNaN(bn)){setVal("bgNight",gcolorToHex(bn))}}' +
'if(!cfg.useGPS)document.getElementById("manualLoc").style.display="block";' +
'if(cfg.useDayNight)document.getElementById("nightColors").style.display="block"}catch(e){}}' +
'function setToggle(id,val){var el=document.getElementById(id);if(val){el.classList.add("on")}}' +
'function setSelect(id,val){if(val!==null&&val!==undefined){document.getElementById(id).value=val}}' +
'function setVal(id,val){document.getElementById(id).value=val}' +
'function save(){var config={};' +
'config.use24h=document.getElementById("toggle24h").classList.contains("on")?1:0;' +
'config.dateFormat=document.getElementById("dateFormat").value;' +
'config.tempUnit=parseInt(document.getElementById("tempUnit").value);' +
'config.useGPS=document.getElementById("toggleGps").classList.contains("on")?1:0;' +
'if(!config.useGPS){' +
'config.latitude=Math.round(parseFloat(document.getElementById("latitude").value||"0")*10000);' +
'config.longitude=Math.round(parseFloat(document.getElementById("longitude").value||"0")*10000);' +
'config.locationStr=document.getElementById("locName").value||""}' +
'config.quadTL=parseInt(document.getElementById("quadTL").value);' +
'config.quadTR=parseInt(document.getElementById("quadTR").value);' +
'config.quadBL=parseInt(document.getElementById("quadBL").value);' +
'config.quadBR=parseInt(document.getElementById("quadBR").value);' +
'config.fgDay=hexToGColor8(document.getElementById("fgDay").value);' +
'config.bgDay=hexToGColor8(document.getElementById("bgDay").value);' +
'config.useDayNight=document.getElementById("toggleDayNight").classList.contains("on")?1:0;' +
'if(config.useDayNight){' +
'config.fgNight=hexToGColor8(document.getElementById("fgNight").value);' +
'config.bgNight=hexToGColor8(document.getElementById("bgNight").value)}' +
'document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify(config))}' +
'window.onload=load<\/script></body></html>';

var cachedSettings = {};

var locationData = {
  lat: null,
  lon: null,
  name: ''
};

function sendError(msg) {
  var d = {}; d['9'] = msg;
  Pebble.sendAppMessage(d);
}

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
        wd['28'] = 77;
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

Pebble.addEventListener('showConfiguration', function(e) {
  var html = CONFIG_HTML;
  if (Object.keys(cachedSettings).length > 0) {
    var configJson = JSON.stringify(cachedSettings);
    html = CONFIG_HTML.replace('window.onload=load', 'var initialConfig=' + configJson + ';window.onload=load');
  }
  var url = 'data:text/html,' + encodeURIComponent(html);
  Pebble.openURL(url);
});

function encodeSettings(config) {
  var buf = [];
  buf.push(2); // version
  buf.push(config.use24h ? 1 : 0);
  var df = config.dateFormat || '';
  for (var i = 0; i < 20; i++) buf.push(i < df.length ? df.charCodeAt(i) : 0);
  buf.push(config.tempUnit || 0);
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

function decodeSettings(buf) {
  var cfg = {}, pos = 0;
  cfg.version = buf[pos++];
  cfg.use24h = buf[pos++];
  var df = '';
  for (var i = 0; i < 20; i++) { var c = buf[pos++]; if (c) df += String.fromCharCode(c); }
  cfg.dateFormat = df;
  cfg.tempUnit = buf[pos++];
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
    } else if (config.latitude && config.longitude) {
      locationData.lat = config.latitude / 10000;
      locationData.lon = config.longitude / 10000;
      locationData.name = config.locationStr || '';
      fetchWeatherFromAPI(locationData.lat, locationData.lon);
    }
  }
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload) {
    if (e.payload.settingsBlob !== undefined) {
      var cfg = decodeSettings(e.payload.settingsBlob);
      for (var k in cfg) {
        cachedSettings[k] = cfg[k];
      }
    }
    if (e.payload.requestWeather !== undefined) {
      if (locationData.lat !== null && locationData.lon !== null) {
        fetchWeatherFromAPI(locationData.lat, locationData.lon);
      } else {
        requestLocationAndFetch();
      }
    }
    if (e.payload.requestSettings !== undefined) {
      if (Object.keys(cachedSettings).length > 0) {
        var qd = {};
        qd['13'] = cachedSettings.quadTL || 0;
        qd['14'] = cachedSettings.quadTR || 0;
        qd['15'] = cachedSettings.quadBL || 0;
        qd['16'] = cachedSettings.quadBR || 0;
        qd['28'] = 99;
        Pebble.sendAppMessage(qd);
      }
    }
  }
});
