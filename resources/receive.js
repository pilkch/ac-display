/*eslint strict: ["error", "global"]*/

import { updateGaugeConfig, drawGaugesWithValues } from './dial.js'; 

let baseUrl;
let socket;

window.addEventListener('load', window_onload);

// This is the main procedure which connects the first socket
function window_onload( /*event*/ )
{
  // Show the disconnected message, it will be hidden if we connect successfully
  showErrorDisconnected();

  // Determine the base url (for http:// this is ws:// for https:// this must be wss://)
  baseUrl = 'ws' + (window.location.protocol === 'https:' ? 's' : '') + '://' + window.location.host + '/ACDisplayServerWebSocket';
  websocket_connect();
}

// This function creates and connects a WebSocket
function websocket_connect()
{
  socket = new WebSocket(baseUrl);
  socket.binaryType = 'arraybuffer';
  socket.onopen    = socket_onopen;
  socket.onclose   = socket_onclose;
  socket.onerror   = socket_onerror;
  socket.onmessage = socket_onmessage;
}

// This is the event when the socket has established a connection
function socket_onopen( /*event*/ )
{
  hideError();
}

// This is the event when the socket has been closed.
function socket_onclose(event)
{
  console.error('WebSocket connection closed: ', event);

  // Show the disconnected message and try connecting again in 1 second
  showErrorDisconnected();
  setTimeout(websocket_connect, 1000);
}

// This is the event when the socket reported an error.
// This will just make an output.
// In the web browser console (F12 on many browsers) will show you more detailed error information.
function socket_onerror(event)
{
  console.error('WebSocket error reported: ', event);
  socket.close();
}

function format_time_smallest_HH_MM_SS_MS(time_ms)
{
  const milliseconds = Math.floor((time_ms % 1000) / 10);
  const seconds = Math.floor((time_ms / 1000) % 60);
  const minutes = Math.floor((time_ms / 1000 / 60) % 60);
  const hours = Math.floor(time_ms / 1000 / 60 / 60);

  let output = "";

  // Hours are optional, only show them when the lap time is large enough
  if (hours != 0) {
    output += hours.toString().padStart(2, '0') + ":";
  }

  const formattedMinutes = minutes.toString().padStart(2, '0');
  const formattedSeconds = seconds.toString().padStart(2, '0');
  const formattedMilliseconds = milliseconds.toString().padStart(2, '0');

  return output + `${formattedMinutes}:${formattedSeconds}.${formattedMilliseconds}`;
}

function format_time_smallest(time_ms)
{
  const milliseconds = Math.floor((time_ms % 1000) / 10);
  const seconds = Math.floor((time_ms / 1000) % 60);
  const minutes = Math.floor((time_ms / 1000 / 60) % 60);
  const hours = Math.floor(time_ms / 1000 / 60 / 60);

  let output = "";

  // Hours are optional, only show them when the lap time is large enough
  if (hours != 0) {
    output += hours.toString().padStart(2, '0') + ":";
  }

  // Minutes are optional, only show them when the lap time is large enough
  if (minutes != 0) {
    output += minutes.toString().padStart(2, '0') + ":";
  }

  const formattedSeconds = seconds.toString().padStart(2, '0');
  const formattedMilliseconds = milliseconds.toString().padStart(2, '0');

  return output + `${formattedSeconds}.${formattedMilliseconds}`;
}

function format_delta_plus_minus_smallest(difference_ms)
{
  if (difference_ms < 0) {
    return "-" + format_time_smallest(-difference_ms);
  }

  return "+" + format_time_smallest(difference_ms);
}

function gear_index_to_letter(gear)
{
  if (gear == 0) return 'R';
  else if (gear == 1) return 'N';

  // Forward gears 1..n
  return gear - 1;
}

let rpm_red_line = 5000.0;
let rpm_maximum = 6000.0;
let speedometer_red_line_kph = 280.0;
let speedometer_maximum_kph = 300.0;

// This is the event when the socket has received a message.
// This will parse the message and execute the corresponding command (or add the message).
function socket_onmessage(event)
{
  if (typeof(event.data) === 'string') {
    // Text message or command
    let message = event.data.split('|', 11);
    switch (message[0]) {
      case 'car_config': {
        rpm_red_line = message[1];
        rpm_maximum = message[2];
        speedometer_red_line_kph = message[3];
        speedometer_maximum_kph = message[4];
        updateGaugeConfig(rpm_red_line, rpm_maximum, speedometer_red_line_kph, speedometer_maximum_kph);

        break;
      }
      case 'car_update': {
        const rpm = message[5];
        const speed_kph = message[6];

        drawGaugesWithValues(rpm, speed_kph);

        //if (digital) {
          const dimColours = [
            "008000",
            "008000",
            "808000",
            "808000",
            "800000",
            "800000",
            "000080",
            "000080"
          ];
          const brightColours = [
            "00ff00",
            "00ff00",
            "ffff00",
            "ffff00",
            "ff0000",
            "ff0000",
            "0000ff",
            "0000ff"
          ];
          for (let i = 0; i < 8; i++) {
            let led = document.getElementById('digital_led' + i);
            let colour = dimColours[i];
            // TODO: Make the first segment always on, then make the others only turn on for the last half of the RPM range, so essentially the lower limits for the segments might be something like:
            // 0.0, 4000.0, 4500.0, 5000.0, 5500.0, 6000.0, 6500.0, 7000.0
            // This would be less distracting and the lights would have a higher resolution for the second half where the shifts actually happen and we want more precision
            const segmentLowerLimit = rpm_red_line / 8.0;
            // If the rpm is past our 1/8th of the gauge then "Turn on" the led by switching from the dim colour to the bright colour
            if (rpm > (segmentLowerLimit * i)) {
              colour = brightColours[i];
            }
            led.setAttribute("style", "background-color: #" + colour);
          }

          let digital_lap = document.getElementById('digital_lap');
          const lap = (message[10] == 0) ? "-" : message[10];
          digital_lap.innerText = `Lap ${lap}`;

          let digital_rpm = document.getElementById('digital_rpm');
          const iRPM = Math.round(rpm);
          digital_rpm.innerText = `${iRPM} RPM`;

          let digital_gear = document.getElementById('digital_gear');
          const gear = gear_index_to_letter(message[1]);
          digital_gear.innerText = `${gear}`;

          let digital_delta = document.getElementById('digital_delta');
          // TODO: We probably need a timer so that for say 5 seconds after a lap is completed we show the delta for the last lap, then it switches to the delta for the current lap
          const best_lap = message[9];
          const delta = message[8] - best_lap;
          const delta_plus_minus_HH_MM_SS_MS = format_delta_plus_minus_smallest(delta);
          digital_delta.innerText = `Delta ${delta_plus_minus_HH_MM_SS_MS}`;

          let digital_last_lap = document.getElementById('digital_last_lap');
          const last_lap_HH_MM_SS_MS = format_time_smallest_HH_MM_SS_MS(message[8]);
          digital_last_lap.innerText = `Last ${last_lap_HH_MM_SS_MS}`;

          //let speed_kph = document.getElementById('speed_kph');
          //speed_kph.setAttribute('value', message[6]);

          //let lap_time_ms = document.getElementById('lap_time_ms');
          //lap_time_ms.setAttribute('value', message[7]);

          //let best_lap_ms = document.getElementById('best_lap_ms');
          //best_lap_ms.setAttribute('value', message[9]);
        //}

          //let accelerator = document.getElementById('accelerator');
          //accelerator.setAttribute('value', message[2]);

          //let brake = document.getElementById('brake');
          //brake.setAttribute('value', message[3]);

          //let clutch = document.getElementById('clutch');
          //clutch.setAttribute('value', message[4]);

        break;
      }
    }
  } else {
    // We received a binary message
  }
}

function showErrorDisconnected()
{
  let errorElement = document.getElementById("errordiv");
  errorElement.style.display = 'block';

  let messageElement = document.getElementById("error");
  messageElement.innerHTML = "Disconnected";
}

function hideError()
{
  let errorElement = document.getElementById("errordiv");
  errorElement.style.display = 'none';
}
