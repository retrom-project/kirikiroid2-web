// Web platform UI. JSPI keeps the TJS caller suspended until an actual answer.
(function () {
    if (typeof document === 'undefined' || typeof window === 'undefined') return;
    var requests = [], active = null, disposed = false, sequence = 0;
    var keys = new Set(), mouse = new Set();
    var getGamepads = navigator.getGamepads ? navigator.getGamepads.bind(navigator) : function () { return []; };
    var style = null;
    var css = `
      .krkr-confirm-dialog { box-sizing:border-box; width:min(640px,calc(100vw - 32px));
        max-width:calc(100vw - 32px); max-height:calc(100dvh - 32px); padding:0; margin:auto;
        overflow:hidden; border:1px solid #4d5266; border-radius:16px; color:#f4f5fa;
        background:#1c1f2c; box-shadow:0 24px 80px #0009; font:15px/1.6 system-ui,sans-serif; }
      .krkr-confirm-dialog[open] { display:flex; flex-direction:column; }
      .krkr-confirm-dialog::backdrop { background:rgba(4,6,12,.72); }
      .krkr-confirm-dialog h2 { flex:none; margin:0; padding:22px 24px 16px;
        font-size:19px; line-height:1.4; overflow-wrap:anywhere; }
      .krkr-confirm-body { min-height:0; overflow:auto; overscroll-behavior:contain;
        padding:0 24px 22px; white-space:pre-wrap; overflow-wrap:anywhere; }
      .krkr-confirm-actions { display:flex; justify-content:flex-end; gap:12px;
        flex:none; padding:16px 24px; background:#151824; border-top:1px solid #353a4d; }
      .krkr-confirm-actions button { min-width:96px; min-height:42px; padding:8px 18px;
        border:1px solid #60677f; border-radius:9px; color:inherit; background:#30364c;
        font:inherit; cursor:pointer; touch-action:manipulation; }
      .krkr-confirm-actions button:focus-visible { outline:3px solid #b6a6ff; outline-offset:3px; }
      .krkr-confirm-actions button[data-selected="true"] { border-color:#b6a6ff; background:#504384; }
      @media (max-height:420px) { .krkr-confirm-dialog h2 { padding:12px 18px 8px; font-size:16px; }
        .krkr-confirm-body { padding:0 18px 12px; font-size:14px; }
        .krkr-confirm-actions { padding:10px 18px; } }
    `;
    function pads() {
        try {
            var source = Module['retromDialogGamepads'];
            return Array.from(typeof source === 'function' ? source() : getGamepads())
                .filter(function (pad) { return pad && pad.connected && pad.mapping === 'standard'; });
        } catch (_) { return []; }
    }
    function pressed(button) { return !!button && (button.pressed || button.value >= .5); }
    function neutral() {
        return !document.hidden && document.hasFocus() && keys.size === 0 && mouse.size === 0 && pads().every(function (pad) {
            return pad.axes.every(function (axis) { return Math.abs(axis) < .35; }) && !pad.buttons.some(pressed);
        });
    }
    function notify(value) {
        if (typeof Module['onConfirmDialogChanged'] === 'function') Module['onConfirmDialogChanged'](value);
    }
    function choose(index) {
        if (!active || active.decision !== null) return;
        active.selected = index;
        active.buttons.forEach(function (button, i) { button.dataset.selected = String(i === index); });
        active.buttons[index].focus({preventScroll:true});
    }
    function decide(index) {
        if (!active || active.decision !== null) return;
        active.decision = index;
        active.node.setAttribute('aria-busy', 'true');
        completeIfNeutral();
    }
    function completeIfNeutral() {
        if (active && active.decision !== null && neutral()) finish(null, active.decision);
    }
    function finish(error, selection) {
        var item = active;
        if (!item) return;
        active = null;
        window.cancelAnimationFrame(item.frame);
        item.node.removeEventListener('close', item.closed);
        if (item.node.open) item.node.close();
        item.node.remove();
        if (!requests.length || disposed) notify(false);
        if (error) item.request.reject(error); else item.request.resolve(selection);
        if (!disposed && !requests.length && Module['canvas'] && Module['canvas'].isConnected)
            Module['canvas'].focus({preventScroll:true});
        queueMicrotask(showNext);
    }
    function showNext() {
        if (disposed || active || !requests.length) return;
        var request = requests.shift(), node = document.createElement('dialog');
        if (!style) { style = document.createElement('style'); style.textContent = css; document.head.append(style); }
        node.className = 'krkr-confirm-dialog';
        node.setAttribute('role', 'alertdialog');
        node.setAttribute('aria-modal', 'true');
        node.dataset.krkrConfirm = '';
        var heading = document.createElement('h2'), body = document.createElement('div');
        var id = 'krkr-confirm-' + (++sequence);
        heading.id = id + '-title'; heading.textContent = request.title;
        body.id = id + '-body'; body.className = 'krkr-confirm-body'; body.tabIndex = 0;
        // Keep every unique word while avoiding a duplicate heading supplied
        // as both the native caption and the first line of the message.
        var message = request.message.replace(/\r\n/g, '\n');
        body.textContent = message.startsWith(request.title + '\n') ? message.slice(request.title.length + 1).trimStart() : message;
        node.setAttribute('aria-labelledby', heading.id); node.setAttribute('aria-describedby', body.id);
        var actions = document.createElement('div'); actions.className = 'krkr-confirm-actions';
        var chinese = /^zh/i.test(navigator.language || ''), buttons = [];
        [chinese ? '确定' : 'OK', chinese ? '取消' : 'Cancel'].forEach(function (label, index) {
            var button = document.createElement('button'); button.type = 'button'; button.textContent = label;
            button.dataset.krkrConfirmChoice = index === 0 ? 'accept' : 'cancel';
            button.addEventListener('click', function () { decide(index); });
            button.addEventListener('focus', function () {
                if (active && active.node === node && active.decision === null) {
                    active.selected = index;
                    buttons.forEach(function (b, i) { b.dataset.selected = String(i === index); });
                }
            });
            buttons.push(button); actions.append(button);
        });
        node.append(heading, body, actions); document.body.append(node);
        active = {request:request, node:node, body:body, buttons:buttons, selected:1, decision:null,
            openingKeys:new Set(keys), openingMouse:new Set(mouse), armed:neutral(), previousButtons:0,
            padIdentity:null, lastTime:0, frame:0, closed:function () { if (active && active.node === node) finish(null, 1); }};
        node.addEventListener('cancel', function (event) { event.preventDefault(); decide(1); });
        node.addEventListener('close', active.closed);
        try { notify(true); node.showModal(); choose(1); active.frame = window.requestAnimationFrame(poll); }
        catch (error) { finish(error); }
    }
    function poll(time) {
        if (!active) return;
        if (document.hidden || !document.hasFocus()) { active.armed = false; }
        else if (active.decision !== null) { completeIfNeutral(); }
        else if (!active.armed) { active.armed = neutral(); }
        else {
            var pad = pads()[0];
            if (pad) {
                var identity = pad.index + ':' + pad.id;
                if (active.padIdentity !== null && identity !== active.padIdentity) active.armed = false;
                active.padIdentity = identity;
                if (active.armed) {
                    if (pressed(pad.buttons[14]) || pad.axes[0] < -.5) choose(0);
                    else if (pressed(pad.buttons[15]) || pad.axes[0] > .5) choose(1);
                    var dy = (pressed(pad.buttons[13]) ? 1 : 0) - (pressed(pad.buttons[12]) ? 1 : 0);
                    if (!dy && Math.abs(pad.axes[1]) > .5) dy = pad.axes[1];
                    active.body.scrollTop += dy * Math.min(40, Math.max(0, time - active.lastTime)) * .5;
                    var bits = (pressed(pad.buttons[0]) ? 1 : 0) | (pressed(pad.buttons[1]) ? 2 : 0);
                    if ((bits & 2) && !(active.previousButtons & 2)) decide(1);
                    else if ((bits & 1) && !(active.previousButtons & 1)) decide(active.selected);
                    if (active) active.previousButtons = bits;
                }
            } else { active.previousButtons = 0; active.padIdentity = null; }
        }
        if (active) { active.lastTime = time; active.frame = window.requestAnimationFrame(poll); }
    }
    function keyboard(event) {
        var up = event.type === 'keyup';
        if (up) keys.delete(event.code); else if (event.type === 'keydown') keys.add(event.code);
        if (!active) return;
        // Release keys held by the game before it requested the modal.
        if (up && active.openingKeys.delete(event.code)) return;
        event.stopImmediatePropagation(); event.preventDefault();
        if (up) { completeIfNeutral(); return; }
        if (event.type !== 'keydown' || event.repeat || !active.armed || active.decision !== null) return;
        if (event.key === 'Escape') decide(1);
        else if (event.key === 'Enter' || event.key === ' ') decide(active.selected);
        else if (event.key === 'ArrowLeft') choose(0);
        else if (event.key === 'ArrowRight') choose(1);
        else if (event.key === 'ArrowUp') active.body.scrollTop -= 48;
        else if (event.key === 'ArrowDown') active.body.scrollTop += 48;
        else if (event.key === 'Tab') {
            var order = [active.body, active.buttons[0], active.buttons[1]], at = order.indexOf(document.activeElement);
            order[(at + (event.shiftKey ? 2 : 1)) % 3].focus({preventScroll:true});
        }
    }
    function pointer(event) {
        if (event.type === 'mousedown') mouse.add(event.button);
        if (event.type === 'mouseup') mouse.delete(event.button);
        if (!active) return;
        if (event.type === 'mouseup' && active.openingMouse.delete(event.button)) return;
        event.stopImmediatePropagation();
        if (!active.node.contains(event.target)) event.preventDefault();
        completeIfNeutral();
    }
    var keyEvents = ['keydown', 'keypress', 'keyup'];
    function blur() { keys.clear(); mouse.clear(); if (active) active.armed = false; }
    var pointerEvents = ['pointerdown', 'pointerup', 'pointermove', 'mousedown', 'mouseup', 'mousemove', 'wheel', 'touchstart', 'touchend'];
    keyEvents.forEach(function (type) { window.addEventListener(type, keyboard, true); });
    pointerEvents.forEach(function (type) { window.addEventListener(type, pointer, {capture:true, passive:false}); });
    window.addEventListener('blur', blur);
    Module['krkr2ShowConfirm'] = function (message, title) {
        if (disposed) return Promise.reject(new DOMException('Runtime disposed', 'AbortError'));
        return new Promise(function (resolve, reject) {
            requests.push({message:message, title:title, resolve:resolve, reject:reject}); showNext();
        });
    };
    Module['krkr2DisposeDialogs'] = function () {
        if (disposed) return;
        disposed = true;
        var error = new DOMException('Runtime disposed', 'AbortError');
        requests.splice(0).forEach(function (request) { request.reject(error); });
        if (active) finish(error);
        if (style) style.remove();
        keyEvents.forEach(function (type) { window.removeEventListener(type, keyboard, true); });
        pointerEvents.forEach(function (type) { window.removeEventListener(type, pointer, true); });
        window.removeEventListener('blur', blur);
        keys.clear(); mouse.clear();
    };
})();
