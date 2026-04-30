// /aoaconfig page client logic, second half.  These handlers wire up
// the form's visibility toggles, curve-type label text, and setpoint
// preview helpers.  Loaded after the form markup so getElementById
// calls resolve.

document.getElementById('id_readEfisData').onchange = function () {
    if (document.getElementById('id_readEfisData').value == 1) {
        [].forEach.call(document.querySelectorAll('.efistypesetting'), function (el) { el.style.visibility = 'visible'; });
    } else {
        [].forEach.call(document.querySelectorAll('.efistypesetting'), function (el) { el.style.visibility = 'hidden'; });
    }
};

document.getElementById('id_casCurveEnabled').onchange = function () {
    if (document.getElementById('id_casCurveEnabled').value == 1) {
        [].forEach.call(document.querySelectorAll('.cascurvesetting'), function (el) { el.style.display = 'block'; });
    } else {
        [].forEach.call(document.querySelectorAll('.cascurvesetting'), function (el) { el.style.display = 'none'; });
    }
};

document.getElementById('id_volumeControl').onchange = function () {
    if (document.getElementById('id_volumeControl').value == 1) {
        [].forEach.call(document.querySelectorAll('.volumepossetting'), function (el) { el.style.display = 'block'; });
        [].forEach.call(document.querySelectorAll('.defaultvolumesetting'), function (el) { el.style.display = 'none'; });
        document.getElementById('volumeControlDiv').classList.remove('flex-col-6');
        document.getElementById('volumeControlDiv').classList.add('flex-col-9');
    } else {
        [].forEach.call(document.querySelectorAll('.volumepossetting'), function (el) { el.style.display = 'none'; });
        [].forEach.call(document.querySelectorAll('.defaultvolumesetting'), function (el) { el.style.display = 'block'; });
        document.getElementById('volumeControlDiv').classList.remove('flex-col-9');
        document.getElementById('volumeControlDiv').classList.add('flex-col-6');
    }
};

document.getElementById('id_overgWarning').onchange = function () {
    if (document.getElementById('id_overgWarning').value == 1) {
        [].forEach.call(document.querySelectorAll('.loadlimitsetting'), function (el) { el.style.display = 'block'; });
    } else {
        [].forEach.call(document.querySelectorAll('.loadlimitsetting'), function (el) { el.style.display = 'none'; });
    }
};

document.getElementById('id_vnoChimeEnabled').onchange = function () {
    if (document.getElementById('id_vnoChimeEnabled').value == 1) {
        [].forEach.call(document.querySelectorAll('.vnochimesetting'), function (el) { el.style.display = 'block'; });
    } else {
        [].forEach.call(document.querySelectorAll('.vnochimesetting'), function (el) { el.style.display = 'none'; });
    }
};

document.getElementById('id_dataSource').onchange = function () {
    if (document.getElementById('id_dataSource').value == 'REPLAYLOGFILE') {
        [].forEach.call(document.querySelectorAll('.replaylogfilesetting'), function (el) { el.style.display = 'block'; });
    } else {
        [].forEach.call(document.querySelectorAll('.replaylogfilesetting'), function (el) { el.style.display = 'none'; });
    }
};

// Upload config file: hand off the file input to a hidden form so the
// surrounding aoaconfigsave POST isn't triggered by the file select.
document.getElementById('id_fileUploadInput').onchange = function () {
    if (document.getElementById('id_fileUploadInput').value.indexOf(".cfg") > 0) {
        document.getElementById('id_realUploadForm').appendChild(document.getElementById('id_fileUploadInput'));
        document.getElementById('id_realUploadForm').action = "/aoaconfigupload";
        document.getElementById('id_realUploadForm').enctype = "multipart/form-data";
        document.getElementById('id_realUploadForm').method = "POST";
        document.getElementById('id_realUploadForm').submit();
    } else {
        alert("Please upload a config file with .cfg extension!");
    }
};

function curveTypeChange(senderId, curveId) {
    if (document.getElementById(senderId).value == 1) {
        document.getElementById('id_aoaCurve' + curveId + 'Param' + 0).innerHTML = ' *X<sup>3</sup>+ ';
        document.getElementById('id_aoaCurve' + curveId + 'Param' + 1).innerHTML = ' *X<sup>2</sup>+ ';
        document.getElementById('id_aoaCurve' + curveId + 'Param' + 2).innerHTML = ' *X<sup></sup>+ ';
        document.getElementById('id_aoaCurve' + curveId + 'Param' + 3).innerHTML = '';
    } else if (document.getElementById(senderId).value == 2) {
        document.getElementById('id_aoaCurve' + curveId + 'Param' + 0).innerHTML = ' * 0 + ';
        document.getElementById('id_aoaCurve' + curveId + 'Param' + 1).innerHTML = ' * 0 + ';
        document.getElementById('id_aoaCurve' + curveId + 'Param' + 2).innerHTML = '*ln(x)+';
        document.getElementById('id_aoaCurve' + curveId + 'Param' + 3).innerHTML = '';
    } else if (document.getElementById(senderId).value == 3) {
        document.getElementById('id_aoaCurve' + curveId + 'Param' + 0).innerHTML = ' * 0 + ';
        document.getElementById('id_aoaCurve' + curveId + 'Param' + 1).innerHTML = ' * 0 + ';
        document.getElementById('id_aoaCurve' + curveId + 'Param' + 2).innerHTML = '* e^ (';
        document.getElementById('id_aoaCurve' + curveId + 'Param' + 3).innerHTML = ' * x)';
    }
}

function cascurveTypeChange(senderId) {
    if (document.getElementById(senderId).value == 1) {
        document.getElementById('id_casCurveParam0').innerHTML = ' *X<sup>3</sup>+ ';
        document.getElementById('id_casCurveParam1').innerHTML = ' *X<sup>2</sup>+ ';
        document.getElementById('id_casCurveParam2').innerHTML = ' *X<sup></sup>+ ';
        document.getElementById('id_casCurveParam3').innerHTML = '';
    } else if (document.getElementById(senderId).value == 2) {
        document.getElementById('id_casCurveParam0').innerHTML = ' * 0 + ';
        document.getElementById('id_casCurveParam1').innerHTML = ' * 0 + ';
        document.getElementById('id_casCurveParam2').innerHTML = '*ln(x)+';
        document.getElementById('id_casCurveParam3').innerHTML = '';
    } else if (document.getElementById(senderId).value == 3) {
        document.getElementById('id_casCurveParam0').innerHTML = ' * 0 + ';
        document.getElementById('id_casCurveParam1').innerHTML = ' * 0 + ';
        document.getElementById('id_casCurveParam2').innerHTML = '* e^ (';
        document.getElementById('id_casCurveParam3').innerHTML = ' * x)';
    }
}

function hasPhysicsModel(flapIdx) {
    var a0 = parseFloat(document.getElementById('id_flapAlpha0' + flapIdx).value);
    var aStall = parseFloat(document.getElementById('id_flapAlphaStall' + flapIdx).value);
    var k = parseFloat(document.getElementById('id_flapKFit' + flapIdx).value);
    return (aStall > a0 + 1.0) && (k > 0);
}
function getAlpha0(flapIdx) { return parseFloat(document.getElementById('id_flapAlpha0' + flapIdx).value); }
function getAlphaStall(flapIdx) { return parseFloat(document.getElementById('id_flapAlphaStall' + flapIdx).value); }
function getKFit(flapIdx) { return parseFloat(document.getElementById('id_flapKFit' + flapIdx).value); }

function kToVs(k, a0, aStall) {
    var range = aStall - a0;
    if (range <= 0 || k <= 0) return null;
    return Math.sqrt(k / range);
}
function aoaToIAS(aoa, a0, k) {
    var d = aoa - a0;
    if (d <= 0 || k <= 0) return null;
    return Math.sqrt(k / d);
}
function aoaToMultiplier(aoa, a0, aStall) {
    var naoa = (aoa - a0) / (aStall - a0);
    if (naoa <= 0.01) return Infinity;
    return 1.0 / Math.sqrt(naoa);
}
function multiplierToAoa(mult, a0, aStall) {
    var naoa = 1.0 / (mult * mult);
    return naoa * (aStall - a0) + a0;
}
function getFlap(flapIdx) {
    return {
        LDMAXAOA: parseFloat(document.getElementById('id_flapLDMAXAOA' + flapIdx).value),
        ONSPEEDFASTAOA: parseFloat(document.getElementById('id_flapONSPEEDFASTAOA' + flapIdx).value),
        ONSPEEDSLOWAOA: parseFloat(document.getElementById('id_flapONSPEEDSLOWAOA' + flapIdx).value),
        STALLWARNAOA: parseFloat(document.getElementById('id_flapSTALLWARNAOA' + flapIdx).value),
        STALLAOA: parseFloat(document.getElementById('id_flapSTALLAOA' + flapIdx).value),
        MANAOA: parseFloat(document.getElementById('id_flapMANAOA' + flapIdx).value)
    };
}
function getAoaBounds(flapIdx, name) {
    var f = getFlap(flapIdx);
    switch (name) {
        case 'LDMAXAOA': return { min: -10, max: f.ONSPEEDFASTAOA - 0.1 };
        case 'ONSPEEDFASTAOA': return { min: f.LDMAXAOA + 0.1, max: f.ONSPEEDSLOWAOA - 0.1 };
        case 'ONSPEEDSLOWAOA': return { min: f.ONSPEEDFASTAOA + 0.1, max: f.STALLWARNAOA - 0.1 };
        case 'STALLWARNAOA': return { min: f.ONSPEEDSLOWAOA + 0.1, max: f.STALLAOA - 0.1 };
        default: return { min: -20, max: 30 };
    }
}
function tapSetpoint(flapIdx, name, dir) {
    var a0 = getAlpha0(flapIdx);
    var aStall = getAlphaStall(flapIdx);
    var field = document.getElementById('id_flap' + name + flapIdx);
    var aoa = parseFloat(field.value);
    if (hasPhysicsModel(flapIdx)) {
        var mult = aoaToMultiplier(aoa, a0, aStall);
        mult += dir * 0.01;
        if (mult < 1.0) mult = 1.0;
        aoa = multiplierToAoa(mult, a0, aStall);
    } else {
        aoa -= dir * 0.10;
    }
    var bounds = getAoaBounds(flapIdx, name);
    aoa = Math.max(bounds.min, Math.min(bounds.max, aoa));
    field.value = aoa.toFixed(2);
    updateSetpointDisplay(flapIdx, name);
}
function updateSetpointDisplay(flapIdx, name) {
    var a0 = getAlpha0(flapIdx);
    var aStall = getAlphaStall(flapIdx);
    var k = getKFit(flapIdx);
    var aoa = parseFloat(document.getElementById('id_flap' + name + flapIdx).value);
    var multEl = document.getElementById('id_mult_' + name + flapIdx);
    var iasEl = document.getElementById('id_ias_' + name + flapIdx);
    if (!multEl || !iasEl) return;
    if (hasPhysicsModel(flapIdx)) {
        var mult = aoaToMultiplier(aoa, a0, aStall);
        var ias = aoaToIAS(aoa, a0, k);
        multEl.textContent = mult.toFixed(2) + ' ×Vs';
        iasEl.textContent = ias ? '(~' + Math.round(ias) + ' kt)' : '';
    } else {
        multEl.textContent = aoa.toFixed(2) + '°';
        iasEl.textContent = '(calibrate for ×Vs display)';
    }
}
function updateDerivedVs(flapIdx) {
    var el = document.getElementById('id_vsInfo' + flapIdx);
    if (!el) return;
    if (hasPhysicsModel(flapIdx)) {
        var vs = kToVs(getKFit(flapIdx), getAlpha0(flapIdx), getAlphaStall(flapIdx));
        el.textContent = vs ? 'Vs = ' + vs.toFixed(1) + ' kt' : '';
    } else {
        el.textContent = '';
    }
}
function updateInfoRow(flapIdx) {
    var stallEl = document.getElementById('id_stallInfo' + flapIdx);
    var manEl = document.getElementById('id_manInfo' + flapIdx);
    if (!stallEl || !manEl) return;
    if (hasPhysicsModel(flapIdx)) {
        var a0 = getAlpha0(flapIdx), aStall = getAlphaStall(flapIdx), k = getKFit(flapIdx);
        var vs = kToVs(k, a0, aStall);
        var stallAoa = parseFloat(document.getElementById('id_flapSTALLAOA' + flapIdx).value);
        stallEl.textContent = 'Stall: ' + stallAoa.toFixed(1) + '°' + (vs ? ' (Vs=' + vs.toFixed(0) + ' kt)' : '');
        var manAoa = parseFloat(document.getElementById('id_flapMANAOA' + flapIdx).value);
        if (manAoa > 0) {
            var va = aoaToIAS(manAoa, a0, k);
            manEl.textContent = 'Maneuvering: ' + manAoa.toFixed(1) + '°' + (va ? ' (Va=' + va.toFixed(0) + ' kt)' : '');
        } else { manEl.textContent = ''; }
    } else {
        stallEl.textContent = '';
        manEl.textContent = '';
    }
}
function initSetpointDisplays() {
    var names = ['LDMAXAOA', 'ONSPEEDFASTAOA', 'ONSPEEDSLOWAOA', 'STALLWARNAOA'];
    for (var i = 0; document.getElementById('id_flapAlpha0' + i); i++) {
        for (var n = 0; n < names.length; n++)
            updateSetpointDisplay(i, names[n]);
        updateDerivedVs(i);
        updateInfoRow(i);
    }
}
initSetpointDisplays();

// Disable Delete Flap Position on Enter key.
document.getElementById("id_configForm").onkeypress = function (e) {
    var key = e.charCode || e.keyCode || 0;
    if (key == 13) {
        e.preventDefault();
    }
};
