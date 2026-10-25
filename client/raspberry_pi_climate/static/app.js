const temperatureElement = document.querySelector("#temperature");
const humidityElement = document.querySelector("#humidity");
const sampleTimeElement = document.querySelector("#sample-time");
const connectionElement = document.querySelector("#connection");
const rangeElement = document.querySelector("#range");
const chartMessageElement = document.querySelector("#chart-message");
const tooltipElement = document.querySelector("#chart-tooltip");
const canvas = document.querySelector("#chart");
const context = canvas.getContext("2d");

const colors = {
  temperature: "#d56443",
  humidity: "#3185a3",
  grid: "#e9eeeb",
  text: "#7b8981",
};

let chartPoints = [];
let chartGeometry = null;

function formatTime(value) {
  return new Date(value).toLocaleString("de-DE", {
    day: "2-digit",
    month: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}

function setConnection(message, isError = false) {
  connectionElement.textContent = message;
  connectionElement.classList.toggle("error", isError);
}

async function loadStatus() {
  try {
    const response = await fetch("/api/v1/status", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`Status HTTP ${response.status}`);
    }
    const data = await response.json();
    const reading = data.reading;
    if (reading) {
      temperatureElement.textContent = `${reading.temperatureC.toFixed(1)} °C`;
      humidityElement.textContent = `${reading.humidityPercent.toFixed(1)} %`;
      sampleTimeElement.textContent = `Letzte Messung: ${formatTime(reading.sampledAt)}`;
    } else {
      temperatureElement.textContent = "—";
      humidityElement.textContent = "—";
      sampleTimeElement.textContent = "Warte auf erste Messung";
    }

    if (data.client.status === "error") {
      setConnection(`Sensorfehler: ${data.client.lastError}`, true);
    } else if (data.client.status === "waiting") {
      setConnection("Warte auf Sensor", false);
    } else {
      setConnection("Verbunden · Messung aktiv");
    }
  } catch (error) {
    setConnection(`Verbindung fehlgeschlagen: ${error.message}`, true);
  }
}

function graphSeries(points) {
  return [
    {
      key: "temperatureC",
      color: colors.temperature,
      unit: "°C",
      min: Math.floor(Math.min(...points.map((point) => point.temperatureC)) - 1),
      max: Math.ceil(Math.max(...points.map((point) => point.temperatureC)) + 1),
    },
    {
      key: "humidityPercent",
      color: colors.humidity,
      unit: "%",
      min: Math.max(
        0,
        Math.floor(Math.min(...points.map((point) => point.humidityPercent)) - 5),
      ),
      max: Math.min(
        100,
        Math.ceil(Math.max(...points.map((point) => point.humidityPercent)) + 5),
      ),
    },
  ];
}

function drawChart(points, selectedIndex = -1) {
  const bounds = canvas.getBoundingClientRect();
  const scale = window.devicePixelRatio || 1;
  canvas.width = Math.round(bounds.width * scale);
  canvas.height = Math.round(bounds.height * scale);
  context.setTransform(scale, 0, 0, scale, 0, 0);

  const width = bounds.width;
  const height = bounds.height;
  const margin = { top: 12, right: 12, bottom: 34, left: 48 };
  const plotWidth = width - margin.left - margin.right;
  const plotHeight = height - margin.top - margin.bottom;
  context.clearRect(0, 0, width, height);
  chartPoints = points;

  if (points.length === 0) {
    chartGeometry = null;
    tooltipElement.hidden = true;
    chartMessageElement.textContent = "Für diesen Zeitraum sind noch keine Messwerte vorhanden.";
    return;
  }
  chartMessageElement.textContent = "";

  const times = points.map((point) => new Date(point.sampledAt).getTime());
  const minTime = Math.min(...times);
  const maxTime = Math.max(...times);
  const timeSpan = Math.max(1, maxTime - minTime);
  const series = graphSeries(points);
  chartGeometry = { margin, plotWidth, plotHeight, minTime, timeSpan, times, series };

  for (let tick = 0; tick <= 4; tick += 1) {
    const x = margin.left + (plotWidth * tick) / 4;
    const y = margin.top + (plotHeight * tick) / 4;
    context.beginPath();
    context.strokeStyle = colors.grid;
    context.moveTo(margin.left, y);
    context.lineTo(width - margin.right, y);
    context.stroke();

    const timestamp = maxTime - (timeSpan * (4 - tick)) / 4;
    const date = new Date(timestamp);
    const tickLabel = Number(rangeElement.value) > 24
      ? date.toLocaleDateString("de-DE", { day: "2-digit", month: "2-digit" })
      : date.toLocaleTimeString("de-DE", { hour: "2-digit", minute: "2-digit" });
    context.fillStyle = colors.text;
    context.font = "11px system-ui, sans-serif";
    context.textAlign = "center";
    context.fillText(tickLabel, x, height - 10);
  }

  for (const item of series) {
    const valueRange = Math.max(1, item.max - item.min);
    for (let tick = 0; tick <= 4; tick += 1) {
      const value = item.max - (valueRange * tick) / 4;
      const y = margin.top + (plotHeight * tick) / 4;
      context.fillStyle = item.color;
      context.font = "10px system-ui, sans-serif";
      context.textAlign = "right";
      context.fillText(`${value.toFixed(0)}${item.unit}`, margin.left - 7, y + 4);
    }

    context.beginPath();
    context.strokeStyle = item.color;
    context.lineWidth = 2;
    points.forEach((point, index) => {
      const x = margin.left + ((times[index] - minTime) / timeSpan) * plotWidth;
      const y =
        margin.top + ((item.max - point[item.key]) / valueRange) * plotHeight;
      if (index === 0) {
        context.moveTo(x, y);
      } else {
        context.lineTo(x, y);
      }
    });
    context.stroke();

    if (selectedIndex >= 0) {
      const selectedPoint = points[selectedIndex];
      const x =
        margin.left +
        ((times[selectedIndex] - minTime) / timeSpan) * plotWidth;
      const y =
        margin.top +
        ((item.max - selectedPoint[item.key]) / valueRange) * plotHeight;
      context.beginPath();
      context.fillStyle = "#fff";
      context.strokeStyle = item.color;
      context.lineWidth = 2;
      context.arc(x, y, 4, 0, Math.PI * 2);
      context.fill();
      context.stroke();
    }
  }

  if (selectedIndex >= 0) {
    const x =
      margin.left +
      ((times[selectedIndex] - minTime) / timeSpan) * plotWidth;
    context.beginPath();
    context.setLineDash([4, 4]);
    context.strokeStyle = "#7b8981";
    context.moveTo(x, margin.top);
    context.lineTo(x, height - margin.bottom);
    context.stroke();
    context.setLineDash([]);
  }
}

function updateTooltip(event) {
  if (!chartGeometry || chartPoints.length === 0) {
    return;
  }

  const pointerX = event.offsetX;
  const { margin, plotWidth, minTime, timeSpan, times } = chartGeometry;
  const hoveredTime = minTime + ((pointerX - margin.left) / plotWidth) * timeSpan;
  let nearestIndex = 0;
  let nearestDistance = Infinity;
  times.forEach((timestamp, index) => {
    const distance = Math.abs(timestamp - hoveredTime);
    if (distance < nearestDistance) {
      nearestDistance = distance;
      nearestIndex = index;
    }
  });

  const point = chartPoints[nearestIndex];
  drawChart(chartPoints, nearestIndex);
  tooltipElement.textContent =
    `${formatTime(point.sampledAt)}\n` +
    `Temperatur: ${point.temperatureC.toFixed(1)} °C\n` +
    `Luftfeuchtigkeit: ${point.humidityPercent.toFixed(1)} %`;
  tooltipElement.hidden = false;
  const tooltipWidth = tooltipElement.offsetWidth;
  const left = Math.min(
    Math.max(8, pointerX + 12),
    canvas.clientWidth - tooltipWidth - 8,
  );
  tooltipElement.style.left = `${left}px`;
  tooltipElement.style.top = "8px";
}

async function loadGraph() {
  try {
    const response = await fetch(
      `/api/v1/readings?hours=${encodeURIComponent(rangeElement.value)}`,
      { cache: "no-store" },
    );
    if (!response.ok) {
      throw new Error(`Messwerte HTTP ${response.status}`);
    }
    const data = await response.json();
    drawChart(data.points);
    if (data.points.length >= data.retainedRecords) {
      chartMessageElement.textContent =
        `Es werden höchstens die letzten ${data.retainedRecords.toLocaleString("de-DE")} Messwerte gespeichert.`;
    }
  } catch (error) {
    chartMessageElement.textContent = `Grafik konnte nicht geladen werden: ${error.message}`;
  }
}

async function refresh() {
  await Promise.all([loadStatus(), loadGraph()]);
}

rangeElement.addEventListener("change", loadGraph);
canvas.addEventListener("pointermove", updateTooltip);
canvas.addEventListener("pointerleave", () => {
  tooltipElement.hidden = true;
  drawChart(chartPoints);
});
window.addEventListener("resize", () => drawChart(chartPoints));
refresh();
window.setInterval(refresh, 10000);
