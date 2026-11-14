// Browser-only User32 boundary. Script callbacks are queued through TVP events.
EM_JS(void, TVPWebDialogDispose, (int id), {
    const state = Module.krkr2Win32Dialogs?.get(id);
    state?.observer?.disconnect();
    state?.root?.remove();
    Module.krkr2Win32Dialogs?.delete(id);
});

EM_JS(char *, TVPWebDialogCommand, (int id, const char *operation, const char *request), {
    try {
        const op = UTF8ToString(operation);
        const input = JSON.parse(UTF8ToString(request)), args = input.args;
        const dialogs = Module.krkr2Win32Dialogs ||= new Map();
        let state = dialogs.get(id);
        if(!state) {
            state = {template:null, root:null, items:new Map(), normal:null};
            dialogs.set(id, state);
        }
        const emit = (kind, wparam, lparam) =>
            Module['_krkr2_web_dialog_event'](id, kind, wparam, lparam);
        const rect = element => {
            const r = element.getBoundingClientRect();
            return {left:Math.round(r.left),top:Math.round(r.top),
                    right:Math.round(r.right),bottom:Math.round(r.bottom)};
        };
        const measure = font => {
            const span = document.createElement('span');
            span.style.cssText = 'all:initial;position:fixed;left:-10000px;white-space:pre;visibility:hidden;';
            span.style.font = font;
            span.textContent = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
            document.body.append(span);
            const r = span.getBoundingClientRect();
            span.remove();
            const h = Math.round(r.width/52), v = Math.ceil(r.height);
            if(h <= 0 || v <= 0) throw Error('Cannot measure dialog font');
            return {h,v};
        };
        const requireOpen = () => {
            if(!state.root) throw Error('Dialog not opened.');
        };
        const constrainWindow = () => {
            if(!state.root) return;
            const r=state.root.getBoundingClientRect();
            state.root.style.left=Math.round(Math.max(0,Math.min(r.left,innerWidth-r.width)))+'px';
            state.root.style.top=Math.round(Math.max(0,Math.min(r.top,innerHeight-r.height)))+'px';
        };
        const item = key => {
            requireOpen();
            if(Number(key) === 0) return state.window;
            const result = state.items.get(Number(key));
            if(!result) throw Error('Dialog control does not exist: '+key);
            return result;
        };
        const getText = control => control === state.window ?
            state.caption.textContent : (control.element.tagName === 'BUTTON' ? control.element.textContent : (control.element.value ?? control.element.textContent));
        const setText = (control, value) => {
            const text = String(value ?? "");
            if(control === state.window) state.caption.textContent = text;
            else if('value' in control.element && control.element.tagName !== 'BUTTON') control.element.value = text;
            else control.element.textContent = text;
        };
        const applyStyle = control => {
            const e = control.element, style = control.style >>> 0;
            e.disabled = !!(style & 0x08000000);
            e.hidden = !(style & 0x10000000);
            if(control.kind === 'EDIT') {
                e.readOnly = !!(style & 0x800);
                e.wrap = (style & 0x80) ? 'off' : 'soft';
                e.style.textAlign = (style & 3) === 1 ? 'center' : (style & 3) === 2 ? 'right' : 'left';
                e.style.overflowX = (style & 0x100000) ? 'auto' : 'hidden';
                e.style.overflowY = (style & 0x200000) ? 'auto' : 'hidden';
            }
        };
        let value;
        if(op === 'makeTemplate') {
            state.template = {header:args[0], items:args.slice(1)};
        } else if(op === 'open') {
            if(state.root) throw Error('Dialog already opened.');
            if(!input.modeless) throw Error('Modal Win32 templates are not supported by this browser backend');
            if(!state.template) throw Error('Dialog template has not been set');
            const {header,items} = state.template;
            if(header.menu) throw Error('Native dialog menus are not supported');
            const root = document.createElement('section');
            root.setAttribute('role','dialog');
            root.setAttribute('aria-label',String(header.title || ""));
            root.dataset.krkrWin32Dialog = id;
            root.style.cssText = 'position:fixed;z-index:2147483000;box-sizing:border-box;border:1px solid #737373;background:#f0f0f0;color:#111;box-shadow:0 6px 24px #0008;min-width:100px;min-height:55px;max-width:100vw;max-height:100vh;resize:both;overflow:hidden;';
            root.style.font = 'caption';
            if((header.style & 0x40) && header.pointSize) {
                root.style.fontSize = Number(header.pointSize)+'pt';
                root.style.fontFamily = header.typeFace ? JSON.stringify(header.typeFace)+',sans-serif' : 'sans-serif';
                root.style.fontWeight = String(header.weight || 400);
                root.style.fontStyle = header.italic ? 'italic' : 'normal';
            }
            document.body.append(root);
            state.units = measure(getComputedStyle(root).font);
            const u = state.units;
            root.style.left = Math.round((header.x || 0)*u.h/4)+'px';
            root.style.top = Math.round((header.y || 0)*u.v/8)+'px';
            root.style.width = Math.max(100,Math.round(header.cx*u.h/4)+2)+'px';
            root.style.height = Math.max(55,Math.round(header.cy*u.v/8)+30)+'px';
            root.tabIndex = -1;
            const bar = document.createElement('div');
            bar.style.cssText = 'height:28px;display:flex;align-items:center;padding-left:8px;box-sizing:border-box;background:#ddd;cursor:move;user-select:none;';
            const caption = document.createElement('span');
            caption.style.cssText = 'flex:1;overflow:hidden;white-space:nowrap;text-overflow:ellipsis;';
            caption.textContent = String(header.title || "");
            const close = document.createElement('button');
            close.textContent = '×'; close.setAttribute('aria-label','关闭');
            close.style.cssText = 'font:inherit;width:32px;height:26px;border:0;background:transparent;color:inherit;cursor:pointer;';
            close.onclick = () => emit(0,2,0);
            bar.append(caption,close);
            const client = document.createElement('div');
            client.style.cssText = 'position:absolute;left:0;top:28px;right:0;bottom:0;overflow:hidden;';
            root.append(bar,client);
            state.root = root; state.caption = caption; state.client = client;
            Module.krkr2Win32NextHandle = (Module.krkr2Win32NextHandle || 100)+1;
            state.window = {element:root,handle:Module.krkr2Win32NextHandle,style:header.style,exStyle:header.exStyle,kind:'DIALOG'};
            const atoms = {128:'BUTTON',129:'EDIT',130:'STATIC',131:'LISTBOX',132:'SCROLLBAR',133:'COMBOBOX'};
            for(const data of items) {
                const kind = atoms[data.windowClass] || String(data.windowClass).toUpperCase();
                let element;
                if(kind === 'EDIT') element = document.createElement((data.style & 4) ? 'textarea' : 'input');
                else if(kind === 'BUTTON') {
                    const type = data.style & 15;
                    if([2,3,4,5,6,9].includes(type)) {
                        element = document.createElement('input');
                        element.type = [4,9].includes(type) ? 'radio' : 'checkbox';
                        element.setAttribute('aria-label',String(data.title || ""));
                    } else {element = document.createElement('button');element.type='button';}
                } else if(kind === 'STATIC') element = document.createElement('div');
                else if(kind === 'LISTBOX' || kind === 'COMBOBOX') {
                    element = document.createElement('select');
                    if(kind === 'LISTBOX') element.size = Math.max(2,Number(data.cy)/10);
                } else throw Error('Unsupported browser dialog control: '+kind);
                element.style.cssText = 'position:absolute;box-sizing:border-box;min-width:0;min-height:0;margin:0;font:inherit;resize:none;';
                element.style.left = Math.round(data.x*u.h/4)+'px';
                element.style.top = Math.round(data.y*u.v/8)+'px';
                element.style.width = Math.max(0,Math.round(data.cx*u.h/4))+'px';
                element.style.height = Math.max(0,Math.round(data.cy*u.v/8))+'px';
                element.dataset.controlId = String(data.id);
                const control = {element,handle:++Module.krkr2Win32NextHandle,style:data.style,exStyle:data.exStyle,kind,modified:false};
                state.items.set(Number(data.id),control);
                setText(control,data.title);
                applyStyle(control);
                element.addEventListener('input',() => {
                    control.modified = true;
                    emit(0,((kind === 'EDIT' ? 0x300 : 1)<<16)|(data.id & 65535),control.handle);
                });
                if(kind === 'BUTTON') element.addEventListener('click',() => emit(0,data.id & 65535,control.handle));
                client.append(element);
            }
            // Keep typing/focus and pointer gestures inside this actual window.
            for(const type of ['keydown','keyup','keypress','mousedown','mouseup','click','pointerdown','pointerup','wheel'])
                root.addEventListener(type,event => {
                    event.stopPropagation();
                    if(type === 'keydown' && event.key === 'Escape') {event.preventDefault();emit(0,2,0);}
                });
            bar.addEventListener('pointerdown',event => {
                if(event.target === close) return;
                const start = rect(root), x = event.clientX, y = event.clientY;
                bar.setPointerCapture(event.pointerId);
                const move = current => {
                    root.style.left = Math.round(start.left+current.clientX-x)+'px';
                    root.style.top = Math.round(start.top+current.clientY-y)+'px';
                    constrainWindow();
                };
                bar.addEventListener('pointermove',move);
                bar.addEventListener('pointerup',() => bar.removeEventListener('pointermove',move),{once:true});
            });
            let previous = "";
            state.observer = new ResizeObserver(() => {
                if(!state.root) return;
                constrainWindow();
                const width = state.client.clientWidth, height = state.client.clientHeight;
                const current = width+':'+height;
                if(previous === current) return;
                previous = current;
                emit(1,0,(height<<16)|(width & 65535));
            });
            state.observer.observe(client);
            constrainWindow();
            value = 0; // CreateDialog modeless success, after the real window exists.
        } else if(op === 'close') {
            state.observer?.disconnect();
            state.root?.remove();state.root=null;state.items.clear();state.window=null;
        } else if(op === 'HWND') value = state.window?.handle || 0;
        else if(op === 'isValid') value = !!state.root;
        else if(['left','top','width','height'].includes(op)) {
            if(!state.root) value = 0;
            else {const r=rect(state.root);value = op==='left'?r.left:op==='top'?r.top:op==='width'?r.right-r.left:r.bottom-r.top;}
        } else if(op === 'getItem') value = item(args[0]).handle;
        else if(op === 'getItemID') {
            requireOpen();
            const found = [...state.items].find(pair=>pair[1].handle===args[0]);
            if(!found) throw Error('Unknown dialog control handle');
            value=found[0];
        } else if(op === 'getItemClassName') value = item(args[0]).kind;
        else if(op === 'getItemText') value = getText(item(args[0]));
        else if(op === 'setItemText') {setText(item(args[0]),args[1]);value=true;}
        else if(op === 'setItemInt') {setText(item(args[0]),Number(args[1])|0);value=true;}
        else if(op === 'getItemInt') {
            const text=getText(item(args[0])).trim();
            if(!/^[+-]?[0-9]+$/.test(text)) throw Error('Dialog control does not contain an integer');
            value=Number(text)|0;
        } else if(op === 'getItemEnabled') value = !item(args[0]).element.disabled;
        else if(op === 'setItemEnabled') {const e=item(args[0]).element;value=!!e.disabled;e.disabled=!args[1];}
        else if(op === 'setItemFocus') {const before=document.activeElement;item(args[0]).element.focus();value=[state.window,...state.items.values()].find(c=>c.element===before)?.handle || 0;}
        else if(op === 'getItemLong' || op === 'setItemLong') {
            const control=item(args[0]), field={[-16]:'style',[-20]:'exStyle'}[args[1]];
            if(args[1]===-12 && op==='getItemLong') value=Number(args[0]);
            else {
                if(!field) throw Error('Unsupported browser window-long index: '+args[1]);
                value=control[field] || 0;
                if(op==='setItemLong') {control[field]=args[2];applyStyle(control);}
            }
        } else if(op === 'setPos' || op === 'setSize' || op === 'setItemPos' || op === 'setItemSize') {
            const isItem=op.startsWith('setItem');
            if(!state.root && !isItem) value=0;
            else {
                const control=isItem?item(args[0]):state.window, offset=isItem?1:0;
                const size=op.endsWith('Size'), e=control.element;
                e.style[size?'width':'left']=Number(args[offset])+'px';
                e.style[size?'height':'top']=Number(args[offset+1])+'px';
                if(control===state.window) constrainWindow();
                value=true;
            }
        } else if(/^getItem(Left|Top|Width|Height)$/.test(op)) {
            const r=rect(item(args[0]).element);
            value=op.endsWith('Left')?r.left:op.endsWith('Top')?r.top:op.endsWith('Width')?r.right-r.left:r.bottom-r.top;
        } else if(op === 'getWindowRect' || op === 'getClientRect') {
            requireOpen();value=op==='getWindowRect'?rect(state.root):{left:0,top:0,right:state.client.clientWidth,bottom:state.client.clientHeight};
        } else if(op === 'mapRect') {
            requireOpen();const r=args[0],u=state.units;
            value={left:Math.round(r.left*u.h/4),top:Math.round(r.top*u.v/8),right:Math.round(r.right*u.h/4),bottom:Math.round(r.bottom*u.v/8)};
        } else if(op === 'getPlacement') {
            if(state.root) {const r=state.normal || rect(state.root);value={flags:0,showCmd:state.normal?3:1,minLeft:-1,minTop:-1,maxLeft:-1,maxTop:-1,normalLeft:r.left,normalTop:r.top,normalRight:r.right,normalBottom:r.bottom};}
        } else if(op === 'setPlacement') {
            requireOpen();const p=args[0];
            Object.assign(state.root.style,{left:p.normalLeft+'px',top:p.normalTop+'px',width:(p.normalRight-p.normalLeft)+'px',height:(p.normalBottom-p.normalTop)+'px'});
            constrainWindow();
            state.root.hidden=p.showCmd===0;value=true;
        } else if(op === 'maximize' || op === 'restoreMaximize') {
            if(state.root) {
                if(op==='maximize') {state.normal ||= rect(state.root);Object.assign(state.root.style,{left:'0px',top:'0px',width:innerWidth+'px',height:innerHeight+'px'});}
                else if(state.normal) {const r=state.normal;Object.assign(state.root.style,{left:r.left+'px',top:r.top+'px',width:(r.right-r.left)+'px',height:(r.bottom-r.top)+'px'});state.normal=null;}
                value=true;
            } else value=false;
        } else if(op === 'setActive') {requireOpen();state.root.focus();value=state.window.handle;}
        else if(op === 'show') {requireOpen();value=!state.root.hidden;state.root.hidden=args[0]===0;}
        else if(op === 'invalidateAll') {requireOpen();void state.root.offsetWidth;value=true;}
        else if(op === 'sendItemMessage') {
            const control=item(args[0]),e=control.element,msg=Number(args[1]),w=Number(args[2]||0),l=args[3];
            switch(msg) {
                case 0xb8: value=control.modified;break;
                case 0xb9: control.modified=!!w;value=0;break;
                case 0xcf: e.readOnly=!!w;value=true;break;
                case 0xb1: e.setSelectionRange(w,Number(l)<0?e.value.length:Number(l));value=0;break;
                case 0xc2: e.setRangeText(String(l??""),e.selectionStart,e.selectionEnd,'end');control.modified=true;value=0;break;
                case 0xba: value=getText(control).split(String.fromCharCode(13)).join("").split(String.fromCharCode(10)).length;break;
                case 0xb7: e.scrollTop=e.scrollHeight;value=0;break;
                case 0xf0: value=e.indeterminate?2:Number(e.checked);break;
                case 0xf1: e.indeterminate=w===2;e.checked=w===1;value=0;break;
                case 0xf5: e.click();value=0;break;
                default: throw Error('Unsupported browser dialog message: '+msg);
            }
        } else throw Error('Unknown browser dialog operation: '+op);
        return stringToNewUTF8(JSON.stringify(value === undefined ? {} : {value}));
    } catch(error) {
        return stringToNewUTF8(JSON.stringify({error:String(error.message || error)}));
    }
});
