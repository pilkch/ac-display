/*eslint strict: ["error", "global"]*/

"use strict";

(function () {
  // resize the canvas to fill browser window dynamically
  window.addEventListener('resize', resizeCanvas, false);

  function resizeCanvas() {
    let canvas = document.getElementById('gauges_canvas');

    // Is that canvas ok?
    if ((canvas === null) || (!canvas.getContext)) {
      alert("Canvas not supported by your browser!");
      return;
    }

    canvas.width = window.innerWidth;
    canvas.height = window.innerHeight;

    // TODO: Redraw on window resize
    // If we try to draw here we get an error because the globals below haven't been created yet
    // Uncaught ReferenceError: can't access lexical declaration 'foregroundColourWhite' before initialization
    //draw();
  }
  
  resizeCanvas();
}());

// This is originally based on the code in this awesome article by Ray Hammond:
// https://geeksretreat.wordpress.com/2012/04/13/making-a-speedometer-using-html5s-canvas/

const startRotationDegrees = -60.0; // Down and to the left
const arcRotationDegrees = 180.0 + 60.0; // Down and to the right
const endRotationDegrees = startRotationDegrees + arcRotationDegrees;

const foregroundColourWhite = 'rgb(220,220,220)';
const foregroundColourRed = 'rgb(255,0,0)';

function createGauge(_redlineRPM, _maximumRPM, majorTicksFrequency, textStep)
{
  // If we divide the steps up too finely then there isn't enough room for the text to be rendered
  while ((_maximumRPM / majorTicksFrequency) > 20.0) {
    majorTicksFrequency *= 2.0;
    textStep *= 2;
  }

  const _nMajorTicks = _maximumRPM / majorTicksFrequency;
  const _nHalfTicks = _maximumRPM / (majorTicksFrequency / 2.0);
  const _nMinorTicks = _maximumRPM / (majorTicksFrequency / 10.0);
  const _arcMajorTickDegrees = arcRotationDegrees / _nMajorTicks;
  const _arcHalfTickDegrees = arcRotationDegrees / _nHalfTicks;
  const _arcMinorTickDegrees = arcRotationDegrees / _nMinorTicks;
  const _startRedlineDegrees = startRotationDegrees + ((_redlineRPM / _maximumRPM) * arcRotationDegrees);

  return {
    iCurrentValue: 0,
    iTargetValue: 0,
    redlineRPM: _redlineRPM, // The start of the red section
    maximumRPM: _maximumRPM, // The actual end of RPM gauge
    nMajorTicks: _nMajorTicks,
    nHalfTicks: _nHalfTicks, // Ticks halfway between the major ticks
    nMinorTicks: _nMinorTicks,
    arcMajorTickDegrees: _arcMajorTickDegrees,
    arcHalfTickDegrees: _arcHalfTickDegrees,
    arcMinorTickDegrees: _arcMinorTickDegrees,
    startRedlineDegrees: _startRedlineDegrees,
    textStep: textStep
  }
}

let gauges = [
  createGauge(6000.0, 7500.0, 1000.0, 1),
  createGauge(250.0, 300.0, 10.0, 10)
];

let job = null;

// Degrees to radians
function degToRad(angle)
{
  return ((angle * Math.PI) / 180.0);
}

function drawLine(options, line)
{
  // Draw a line using the line object passed in
  options.ctx.beginPath();

  // Set attributes of open
  options.ctx.globalAlpha = line.alpha;
  options.ctx.lineWidth = line.lineWidth;
  options.ctx.fillStyle = line.fillStyle;
  options.ctx.strokeStyle = line.fillStyle;
  options.ctx.moveTo(line.from.X,
    line.from.Y);

  // Plot the line
  options.ctx.lineTo(
    line.to.X,
    line.to.Y
  );

  options.ctx.stroke();
}

function createLine(fromX, fromY, toX, toY, fillStyle, lineWidth, alpha)
{
  // Create a line object using Javascript object notation
  return {
    from: {
      X: fromX,
      Y: fromY
    },
    to:	{
      X: toX,
      Y: toY
    },
    fillStyle: fillStyle,
    lineWidth: lineWidth,
    alpha: alpha
  };
}

function drawOuterMetallicArc(options)
{
  /* Draw the metallic border of the gauge 
  * Outer grey area
  */
  options.ctx.beginPath();

  // Nice shade of grey
  options.ctx.globalAlpha = 0.5;
  options.ctx.fillStyle = "rgb(127,127,127)";

  // Draw the outer circle
  options.ctx.arc(options.center.X,
    options.center.Y,
    options.outerRadius,
    -Math.PI,
    Math.PI,
  );

  // Fill the last object
  options.ctx.fill();
}

function drawInnerMetallicArc(options)
{
  /* Draw the metallic border of the gauge 
  * Inner white area
  */

  options.ctx.beginPath();

  // White
  options.ctx.globalAlpha = 0.5;
  options.ctx.fillStyle = foregroundColourWhite;

  // Outer circle (subtle edge in the grey)
  options.ctx.arc(options.center.X,
    options.center.Y,
    (options.outerRadius / 100) * 90,
    -Math.PI,
    Math.PI,
  );

  options.ctx.fill();
}

function drawMetallicArc(options)
{
  /* Draw the metallic border of the gauge
  * by drawing two semi-circles, one over lapping
  * the other with a bit of alpha transparency
  */

  drawOuterMetallicArc(options);
  drawInnerMetallicArc(options);
}

function drawBackground(options)
{
  /* Black background with alphs transparency to
  * blend the edges of the metallic edge and
  * black background
  */
  let i = 0;

  options.ctx.globalAlpha = 0.2;
  options.ctx.fillStyle = "rgb(0,0,0)";

  const innerRadius = 0.95 * options.gaugeOptions.radius;
  const outerRadius = options.gaugeOptions.radius;

  // Draw semi-transparent circles
  for (i = innerRadius; i < outerRadius; i++) {
    options.ctx.beginPath();

    options.ctx.arc(options.center.X,
      options.center.Y,
      i,
      -Math.PI,
      Math.PI
    );

    options.ctx.fill();
  }
}

function applyDefaultContextSettings(options)
{
  /* Helper function to revert to gauges
  * default settings
  */

  options.ctx.lineWidth = 2;
  options.ctx.globalAlpha = 1.0;
  options.ctx.strokeStyle = foregroundColourWhite;
  options.ctx.fillStyle = foregroundColourWhite;
}

/*function drawSmallTickMarks(options, gauge)
{
  let iTick = 0,
      iTickRad = 0,
      fromX,
      fromY,
      line,
    toX,
    toY;

  applyDefaultContextSettings(options);

  for (iTick = startRotationDegrees; iTick < endRotationDegrees; iTick += gauge.arcMinorTickDegrees) {

    iTickRad = degToRad(iTick);

    // Calculate the X and Y of both ends of the
    // line I need to draw at angle represented at Tick.
    // The aim is to draw the a line starting on the 
    // coloured arc and continueing towards the outer edge
    // in the direction from the center of the gauge. 

    fromX = options.center.X - (Math.cos(iTickRad) * options.levelRadius);
    fromY = options.center.Y - (Math.sin(iTickRad) * options.levelRadius);
    toX = options.center.X - (Math.cos(iTickRad) * options.gaugeOptions.radius);
    toY = options.center.Y - (Math.sin(iTickRad) * options.gaugeOptions.radius);

    // Create a line expressed in JSON
    line = createLine(fromX, fromY, toX, toY, (iTick >= gauge.startRedlineDegrees) ? foregroundColourRed : foregroundColourWhite, 3, 1.0);

    // Draw the line
    drawLine(options, line);
  }
}*/

function drawHalfTickMarks(options, gauge)
{
  let iTick = 0,
      iTickRad = 0,
      fromX,
      fromY,
      line,
    toX,
    toY;
  const tickInnerRadius = options.levelRadius - 8;

  applyDefaultContextSettings(options);

  for (iTick = startRotationDegrees; iTick < endRotationDegrees; iTick += gauge.arcHalfTickDegrees) {

    iTickRad = degToRad(iTick);

    /* Calculate the X and Y of both ends of the
    * line I need to draw at angle represented at Tick.
    * The aim is to draw the a line starting on the 
    * coloured arc and continueing towards the outer edge
    * in the direction from the center of the gauge. 
    */

    fromX = options.center.X - (Math.cos(iTickRad) * options.levelRadius);
    fromY = options.center.Y - (Math.sin(iTickRad) * options.levelRadius);
    toX = options.center.X - (Math.cos(iTickRad) * tickInnerRadius);
    toY = options.center.Y - (Math.sin(iTickRad) * tickInnerRadius);

    // Create a line expressed in JSON
    line = createLine(fromX, fromY, toX, toY, (iTick >= gauge.startRedlineDegrees) ? foregroundColourRed : foregroundColourWhite, 3, 1.0);

    // Draw the line
    drawLine(options, line);
  }
}

function drawLargeTickMarks(options, gauge)
{
  let iTick = 0,
        iTickRad = 0,
        fromX,
        fromY,
        toX,
        toY,
        line;
  const tickInnerRadius = options.levelRadius - 12;

  applyDefaultContextSettings(options);

  for (iTick = startRotationDegrees; iTick < endRotationDegrees; iTick += gauge.arcMajorTickDegrees) {
    iTickRad = degToRad(iTick);

    /* Calculate the X and Y of both ends of the
    * line I need to draw at angle represented at Tick.
    * The aim is to draw the a line starting on the 
    * coloured arc and continueing towards the outer edge
    * in the direction from the center of the gauge. 
    */

    fromX = options.center.X - (Math.cos(iTickRad) * options.levelRadius);
    fromY = options.center.Y - (Math.sin(iTickRad) * options.levelRadius);
    toX = options.center.X - (Math.cos(iTickRad) * tickInnerRadius);
    toY = options.center.Y - (Math.sin(iTickRad) * tickInnerRadius);

    // Create a line expressed in JSON
    line = createLine(fromX, fromY, toX, toY, (iTick >= gauge.startRedlineDegrees) ? foregroundColourRed : foregroundColourWhite, 3, 1.0);

    // Draw the line
    drawLine(options, line);
  }
}

function drawTicks(options, gauge)
{
  // TODO: Are the small ticks slowing down rendering?
  //drawSmallTickMarks(options, gauge);
  drawHalfTickMarks(options, gauge);
  drawLargeTickMarks(options, gauge);
}

function drawTextMarkers(options, gauge)
{
  /* The text labels marks above the coloured
  * arc drawn every 10 mph from 10 degrees to
  * 170 degrees.
  */
  let iTick = 0.0,
      iTickToPrint = 0;
  const textRadius = 0.8 * options.gaugeOptions.radius;

  applyDefaultContextSettings(options);

  // Font styling
  options.ctx.font = '1.5vw sans-serif';
  options.ctx.textBaseline = 'top';
  options.ctx.fillStyle = foregroundColourWhite;

  options.ctx.beginPath();

  let bIntoRedLine = false;

  for (iTick = startRotationDegrees; iTick < endRotationDegrees; iTick += gauge.arcMajorTickDegrees) {

    if (!bIntoRedLine && (iTick >= gauge.startRedlineDegrees)) {
      options.ctx.fillStyle = foregroundColourRed;

      bIntoRedLine = true;
    }

    let dimensions = options.ctx.measureText(iTickToPrint);

    options.ctx.fillText(iTickToPrint,
      (options.center.X - (0.5 * dimensions.width)) - (Math.cos(degToRad(iTick)) * textRadius),
      (options.center.Y - (0.5 * dimensions.emHeightDescent)) - (Math.sin(degToRad(iTick)) * textRadius)
    );

    // Round the numbers for printing
    iTickToPrint += gauge.textStep;
  }

  options.ctx.stroke();
}

function drawLineArc(options, gauge)
{
  // Red section
  options.ctx.beginPath();

  options.ctx.globalAlpha = 1.0;
  options.ctx.lineWidth = 5;
  options.ctx.strokeStyle = foregroundColourRed;

  options.ctx.arc(options.center.X,
    options.center.Y,
    options.levelRadius,
    degToRad(180.0 + gauge.startRedlineDegrees), degToRad(180.0 + endRotationDegrees)
  );

  options.ctx.stroke();

  // White section
  options.ctx.beginPath();

  options.ctx.globalAlpha = 1.0;
  options.ctx.lineWidth = 5;
  options.ctx.strokeStyle = foregroundColourWhite;

  options.ctx.arc(options.center.X,
    options.center.Y,
    options.levelRadius,
    degToRad(180.0 + startRotationDegrees), degToRad(180.0 + gauge.startRedlineDegrees)
  );

  options.ctx.stroke();
}

function drawNeedleDial(options, alphaValue, strokeStyle, fillStyle)
{
  /* Draws the metallic dial that covers the base of the
  * needle.
  */
  let i = 0;

  options.ctx.globalAlpha = alphaValue;
  options.ctx.lineWidth = 3;
  options.ctx.strokeStyle = strokeStyle;
  options.ctx.fillStyle = fillStyle;

  // Draw several transparent circles with alpha
  for (i = 0; i < options.needleBaseRadius; i++) {

    options.ctx.beginPath();
    options.ctx.arc(options.center.X,
      options.center.Y,
      i,
      -Math.PI,
      Math.PI
    );

    options.ctx.fill();
    options.ctx.stroke();
  }
}

function drawNeedle(options, gauge)
{
  // Draw the needle in a nice red colour at the angle that represents the value of value

  const iValueAsAngle = startRotationDegrees + ((gauge.iTargetValue / gauge.maximumRPM) * arcRotationDegrees);
  const iValueAsAngleRad = degToRad(iValueAsAngle);

  const fromX = options.center.X - (Math.cos(iValueAsAngleRad) * options.needleBaseRadius);
  const fromY = options.center.Y - (Math.sin(iValueAsAngleRad) * options.needleBaseRadius);
  const toX = options.center.X - (Math.cos(iValueAsAngleRad) * options.needleLength);
  const toY = options.center.Y - (Math.sin(iValueAsAngleRad) * options.needleLength);

  let line = createLine(fromX, fromY, toX, toY, foregroundColourRed, 5, 1.0);

  drawLine(options, line);

  // Two circle to draw the dial at the base (give its a nice effect?)
  drawNeedleDial(options, 0.6, "rgb(127, 127, 127)", "rgb(255,255,255)");
  drawNeedleDial(options, 0.2, "rgb(127, 127, 127)", "rgb(127,127,127)");
}

function buildOptionsAsJSON(canvas)
{
  // Setting for the gauge 
  // Alter these to modify its look and feel

  const centerX = canvas.width / 2;
  const centerY = canvas.height / 2;
  const outerRadius = 0.7 * (canvas.width / 4);
  const radius = (170 / 200) * outerRadius;
  const levelRadius = 0.93 * radius; // The white and red arc
  const needleBaseRadius = 0.1 * radius;
  const needleLength = 0.9 * levelRadius;

  // Create an options object using Javascript object notation
  return {
    ctx: canvas.getContext('2d'),
    center:	{
      X: centerX,
      Y: centerY
    },
    levelRadius: levelRadius,
    needleBaseRadius: needleBaseRadius,
    needleLength: needleLength,
    gaugeOptions: {
      radius: radius
    },
    outerRadius: outerRadius
  };
}

function clearCanvas(options)
{
  options.ctx.clearRect(0, 0, options.ctx.width, options.ctx.height);
  applyDefaultContextSettings(options);
}

function drawGauge(gauge, options)
{
  // Sanity checks
  if (isNaN(gauge.iTargetValue)) {
    gauge.iTargetValue = 0;
  } else if (gauge.iTargetValue < 0) {
    gauge.iTargetValue = 0;
  }

  // Draw the metallic styled edge
  drawMetallicArc(options);

  // Draw thw background
  drawBackground(options);

  // Draw tick marks
  drawTicks(options, gauge);

  // Draw labels on markers
  drawTextMarkers(options, gauge);

  // Draw the white and red arcs
  drawLineArc(options, gauge);

  // Draw the needle and base
  drawNeedle(options, gauge);

  gauge.iCurrentValue = gauge.iTargetValue;
}

function draw()
{
  // Reset the timer
  clearTimeout(job);

  /* Main entry point for drawing the gauge
  * If canvas is not support alert the user.
  */

  let canvas = document.getElementById('gauges_canvas');

  // Canvas good?
  if ((canvas === null) || (!canvas.getContext)) {
    alert("Canvas not supported by your browser!");
    return;
  }

  let options = buildOptionsAsJSON(canvas);

  // Clear canvas
  clearCanvas(options);

  for (let i = 0; i < gauges.length; i++) {
    // TODO: Fix this ugliness
    if (i == 0) {
      options.center.X = canvas.width / 4;
    } else {
      options.center.X = 3 * (canvas.width / 4);
    }

    let gauge = gauges[i];
    drawGauge(gauge, options);
  }
}


export function updateGaugeConfig(rpm_red_line, rpm_maximum, speedometer_red_line_kph, speedometer_maximum_kph)
{
  gauges[0] = createGauge(rpm_red_line, rpm_maximum, 1000.0, 1);
  gauges[1] = createGauge(speedometer_red_line_kph, speedometer_maximum_kph, 10.0, 10);
}

export function drawGaugesWithValues(rpm, speed_kph)
{
  gauges[0].iTargetValue = rpm;
  gauges[1].iTargetValue = speed_kph;

  job = setTimeout(draw, 5);
}
