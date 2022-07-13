function generateBoxShadow(maxHeight, step, threshold) {
    var r = '';
    var thPx = threshold * maxHeight;
    for (var i = 0; i <= maxHeight; i += step) {
        if (i !== 0) {
            r += ', ';
        }
        r += 0 + 'px ' + i + 'px ' + (i + step > thPx && i <= thPx ? 3 : 0) + 'px ' + (i <= thPx ? '#ff0000' : '#115599') + Math.max(Math.min(192, 12 * step), 16).toString(16);
    }
    return r;
}
function drawCylinder(heightCylinder, stepPx, cylinderWarm) {
    var x = generateBoxShadow(heightCylinder, stepPx, cylinderWarm);
    $('#top')[0].style.boxShadow = x;
    $('#bottom')[0].style.marginTop = heightCylinder - 100 + 'px';
}
function setMetrics(cylinderWarm, cylinderTime) {
    $('#boilerPercent')[0].innerText = (cylinderWarm*100).toFixed(2);
    $('#boilerTime')[0].innerText = (cylinderTime).toFixed(0);
}
function update(gHeightPx, gStepPx) {
    fetch("/api/BoilerData")
        .then(response => {
            var statusPromise;
            if (response.ok) {
                statusPromise = response.json()
                    .then(json => {
                        var cylinderWarm = json.boilerWarm;
                        var cylinderTime = json.boilerTime;
                        drawCylinder(gHeightPx, gStepPx, cylinderWarm);
                        setMetrics(cylinderWarm, cylinderTime);
                        return Promise.resolve(`Обновлено: клиент: ${new Date().toLocaleString()}, сервер: ${new Date(json.lastNotified).toLocaleString()}`);
                    });
            } else {
                statusPromise = response.text()
                    .then(text => {
                        return Promise.resolve(`Обновлено: клиент: ${new Date().toLocaleString()}, ошибка HTTP ${response.status} ${response.statusText}: [${text}]`);
                    });
            }
            return statusPromise;
        })
        .then(s => $('#loader')[0].innerText = s);
}
