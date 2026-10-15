// Runs in WhatsApp's page. It owns navigation and geometry only: the actual
// settings form lives in a separate, trusted Qt WebEngine view.
(function () {
    'use strict';

    if (window.__whatlySettingsNavigation) {
        window.__whatlySettingsNavigation.refresh();
        return;
    }

    const ROW_ID = 'whatly-settings-row';
    const PANEL_ID = 'whatly-settings-placeholder';
    const STATUS_ID = 'whatly-settings-status';
    const ACTIONS = 'button,[role="button"],[role="menuitem"],a,[role="tab"]';
    const ICONS = '[data-icon],[data-testid],[data-action]';
    const LOGOUT = /(?:^|[-_])(?:log[\s_-]?out|sign[\s_-]?out|exit)(?:$|[-_])/i;
    const SETTINGS = /(?:^|[-_])settings(?:$|[-_])/i;
    const LOGOUT_LABEL = /^(?:log out|logout|sign out|se déconnecter|déconnexion|abmelden|cerrar sesión|sair|esci|disconnetti|uitloggen|ログアウト|로그아웃|退出登录|登出|लॉग आउट|लग आउट|تسجيل الخروج|выход|выйти|çıkış yap|keluar)$/i;
    const SETTINGS_LABEL = /^(?:settings|paramètres|einstellungen|ajustes|configuración|configurações|impostazioni|instellingen|設定|설정|设置|設定|सेटिंग्स|सेटिङहरू|الإعدادات|настройки|ayarlar|pengaturan)$/i;
    const SETTINGS_ICON = /(?:^|[-_])(?:account|privacy|notification|notifications|chat|chats|help|storage|lock|user)(?:$|[-_])/i;
    const owned = element => !!(element && element.closest && element.closest('[data-whatly-settings-owned]'));
    const text = element => (element.getAttribute('aria-label') || element.textContent || '').trim().replace(/\s+/g, ' ');
    let context = null;
    let row = null;
    let placeholder = null;
    let active = false;
    let scheduled = 0;
    let openTimer = 0;
    let hostTimer = 0;
    let activeTimer = 0;
    let lastHost = null;
    let lastPayload = '';
    let pendingState = { visible: false };
    let openDeadline = 0;
    let hostDeadline = 0;
    let columnObserver = null;

    function rendered(element) {
        if (!element || !element.isConnected || owned(element)) return false;
        const rect = element.getBoundingClientRect();
        if (rect.width < 1 || rect.height < 1) return false;
        const style = getComputedStyle(element);
        return style.visibility !== 'hidden' && style.display !== 'none' &&
            !element.closest('[aria-hidden="true"],[inert]');
    }

    function tokenMatches(element, expression) {
        return ['data-icon', 'data-testid', 'data-action'].some(name =>
            expression.test(element.getAttribute(name) || ''));
    }

    function railSettings() {
        const marked = Array.from(document.querySelectorAll(ICONS))
            .filter(element => tokenMatches(element, SETTINGS))
            .map(element => element.closest(ACTIONS));
        const labelled = Array.from(document.querySelectorAll(ACTIONS))
            .filter(element => SETTINGS_LABEL.test(text(element)));
        return marked.concat(labelled).find(element => {
            if (!rendered(element)) return false;
            const rect = element.getBoundingClientRect();
            return rect.width <= 100 && (rect.left < 100 || rect.right > innerWidth - 100);
        }) || null;
    }

    // An exit icon also occurs in menus and confirmation dialogs. Require a
    // settings context before adding anything, and never alter dialog content.
    function settingsContext(list, nav) {
        if (nav && ['aria-pressed', 'aria-selected'].some(name => nav.getAttribute(name) === 'true')) return true;
        if (nav && nav.getAttribute('aria-current') === 'page') return true;
        if (/settings/i.test(list.getAttribute('data-testid') || '')) return true;
        const rect = list.getBoundingClientRect();
        const heading = Array.from(document.querySelectorAll('h1,h2,[role="heading"]')).some(element => {
            if (!rendered(element) || !SETTINGS_LABEL.test(text(element))) return false;
            const r = element.getBoundingClientRect();
            return r.left >= rect.left - 8 && r.right <= rect.right + 8;
        });
        if (heading) return true;
        return Array.from(list.querySelectorAll(ICONS)).filter(element =>
            tokenMatches(element, SETTINGS_ICON)).length >= 2;
    }

    function columnFor(list) {
        let column = list;
        const initial = list.getBoundingClientRect();
        for (let parent = list.parentElement; parent && parent !== document.body; parent = parent.parentElement) {
            const rect = parent.getBoundingClientRect();
            if (Math.abs(rect.left - initial.left) > 24 || Math.abs(rect.width - initial.width) > 64) break;
            column = parent;
        }
        return column;
    }

    function locate() {
        const nav = railSettings();
        const marked = Array.from(document.querySelectorAll(ICONS)).filter(element => tokenMatches(element, LOGOUT));
        const labelled = Array.from(document.querySelectorAll(ACTIONS)).filter(element => LOGOUT_LABEL.test(text(element)));
        for (const marker of marked.concat(labelled)) {
            if (!rendered(marker) || marker.closest('[role="dialog"],[aria-modal="true"]')) continue;
            let action = marker.closest(ACTIONS);
            if (!action) continue;
            const actionRect = action.getBoundingClientRect();
            if (actionRect.width < 130 || actionRect.height < 20 || actionRect.height > 130) continue;

            // Find the level where complete setting rows are siblings. Inserting
            // into a logout button's inner wrapper would squeeze both controls.
            let anchor = action;
            for (let depth = 0; depth < 7 && anchor.parentElement; depth++, anchor = anchor.parentElement) {
                const list = anchor.parentElement;
                if (list === document.body || list.id === 'app') break;
                const r = list.getBoundingClientRect();
                if (r.width > Math.max(620, innerWidth * 0.8)) break;
                const neighbours = Array.from(list.querySelectorAll(ACTIONS)).filter(element =>
                    !anchor.contains(element) && !owned(element) && rendered(element));
                if (!neighbours.length || !settingsContext(list, nav)) continue;
                const template = neighbours.find(element => {
                    const b = element.getBoundingClientRect();
                    return b.width >= 130 && b.height >= 24 && b.height <= 110;
                }) || action;
                return { list, anchor, action, template, column: columnFor(list), nav };
            }
        }
        return null;
    }

    function opaqueBackground(element) {
        for (let node = element; node; node = node.parentElement) {
            const color = getComputedStyle(node).backgroundColor;
            if (color && color !== 'transparent' && !/rgba\([^)]*,\s*0\s*\)/.test(color)) return color;
        }
        return document.documentElement.classList.contains('dark') || document.body.classList.contains('dark') ? '#111b21' : '#ffffff';
    }

    function appearance(element) {
        const style = getComputedStyle(element);
        const background = opaqueBackground(element);
        const rgb = background.match(/\d+(?:\.\d+)?/g);
        const darkClass = document.documentElement.classList.contains('dark') || document.body.classList.contains('dark');
        const dark = rgb && rgb.length >= 3 ? (+rgb[0] * 0.2126 + +rgb[1] * 0.7152 + +rgb[2] * 0.0722 < 128) : darkClass;
        const color = (name, fallback) => {
            const value = style.getPropertyValue(name).trim();
            return value && CSS.supports('color', value) ? value : fallback;
        };
        return {
            background,
            foreground: style.color || (dark ? '#e9edef' : '#111b21'),
            muted: color('--secondary', dark ? '#8696a0' : '#667781'),
            accent: color('--button-primary-background', dark ? '#00a884' : '#008069'),
            fontFamily: style.fontFamily,
            mode: dark ? 'dark' : 'light'
        };
    }

    function paintRow() {
        if (!row || !context) return;
        const source = getComputedStyle(context.template);
        const colors = appearance(context.list);
        const properties = {
            fontFamily: source.fontFamily,
            fontSize: source.fontSize,
            lineHeight: source.lineHeight,
            color: colors.foreground,
            backgroundColor: active ? (colors.mode === 'dark' ? '#202c33' : '#f0f2f5') : 'transparent'
        };
        for (const [key, value] of Object.entries(properties)) {
            if (row.style[key] !== value) row.style[key] = value;
        }
        row.setAttribute('aria-pressed', active ? 'true' : 'false');
    }

    function addRow(next) {
        if (row && row.isConnected && row.parentElement === next.list && row.nextElementSibling === next.anchor) {
            context = next;
            return;
        }
        if (active) close();
        if (row) row.remove();
        context = next;
        row = document.createElement('button');
        row.id = ROW_ID;
        row.type = 'button';
        row.setAttribute('data-whatly-settings-owned', 'row');
        row.setAttribute('aria-label', 'Whatly Settings');
        row.style.cssText = 'box-sizing:border-box;display:flex;align-items:center;gap:24px;width:100%;min-height:60px;padding:14px 24px;border:0;border-radius:0;text-align:start;cursor:pointer;flex-shrink:0;';
        const icon = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        icon.setAttribute('viewBox', '0 0 24 24');
        icon.setAttribute('width', '24');
        icon.setAttribute('height', '24');
        icon.setAttribute('aria-hidden', 'true');
        icon.style.flexShrink = '0';
        icon.innerHTML = '<path fill="currentColor" d="m19.4 13 .1-1-.1-1 2-1.5-2-3.4-2.3.9a8 8 0 0 0-1.7-1L15 3h-4l-.4 3a8 8 0 0 0-1.7 1l-2.3-.9-2 3.4L6.6 11l-.1 1 .1 1-2 1.5 2 3.4 2.3-.9a8 8 0 0 0 1.7 1l.4 3h4l.5-3a8 8 0 0 0 1.7-1l2.3.9 2-3.4-2.1-1.5ZM13 16a4 4 0 1 1 0-8 4 4 0 0 1 0 8Z"/>';
        const label = document.createElement('span');
        label.textContent = 'Whatly Settings';
        row.append(icon, label);
        row.addEventListener('click', event => {
            event.preventDefault();
            event.stopPropagation();
            openPanel();
        });
        row.addEventListener('pointerenter', () => {
            if (!active) row.style.backgroundColor = appearance(context.list).mode === 'dark' ? '#202c33' : '#f0f2f5';
        });
        row.addEventListener('pointerleave', paintRow);
        next.list.insertBefore(row, next.anchor);
        paintRow();
        if (columnObserver) columnObserver.disconnect();
        if (typeof ResizeObserver !== 'undefined') {
            columnObserver = new ResizeObserver(schedule);
            columnObserver.observe(next.column);
        }
    }

    function contentRect() {
        if (!context) return null;
        const column = context.column.getBoundingClientRect();
        const app = document.getElementById('app') || document.documentElement;
        const outer = app.getBoundingClientRect();
        const bounds = {
            left: Math.max(0, outer.left), top: Math.max(0, outer.top),
            right: Math.min(innerWidth, outer.right), bottom: Math.min(innerHeight, outer.bottom)
        };
        const candidates = new Set(document.querySelectorAll('#main,main,[role="main"],[data-testid="settings-content"],[data-testid="conversation-panel-wrapper"]'));
        for (let parent = context.column.parentElement; parent && parent !== document.body; parent = parent.parentElement) {
            for (const child of parent.children) {
                if (!child.contains(context.column)) candidates.add(child);
            }
            if (parent === app) break;
        }
        let best = null;
        let area = 0;
        for (const element of candidates) {
            if (!rendered(element) || context.column.contains(element) || element.contains(context.column)) continue;
            const r = element.getBoundingClientRect();
            if (r.width < 220 || r.height < 180) continue;
            if (r.left < column.right - 3 && r.right > column.left + 3) continue;
            if (r.bottom <= column.top || r.top >= column.bottom) continue;
            const current = r.width * r.height;
            if (current > area) { area = current; best = r; }
        }
        if (!best) {
            const right = bounds.right - column.right;
            const left = column.left - bounds.left;
            if (Math.max(right, left) >= 220) {
                best = right >= left ? { left: column.right, right: bounds.right, top: bounds.top, bottom: bounds.bottom } :
                    { left: bounds.left, right: column.left, top: bounds.top, bottom: bounds.bottom };
            } else {
                // Narrow windows have a single settings column. Use that column
                // while leaving the native nav rail accessible; the trusted form
                // supplies its own back/close control.
                best = column;
            }
        }
        const x = Math.max(bounds.left, best.left);
        const y = Math.max(bounds.top, best.top);
        const width = Math.min(bounds.right, best.right) - x;
        const height = Math.min(bounds.bottom, best.bottom) - y;
        return width >= 180 && height >= 120 ? { x, y, width, height } : null;
    }

    function deliver() {
        const host = window.__whatlySettingsHost;
        if (!host || typeof host.updatePanel !== 'function') return false;
        const encoded = JSON.stringify(pendingState);
        if (host !== lastHost || encoded !== lastPayload) {
            host.updatePanel(pendingState);
            lastHost = host;
            lastPayload = encoded;
        }
        return true;
    }

    function report(state) {
        pendingState = state;
        try {
            if (deliver()) {
                clearTimeout(hostTimer);
                hostTimer = 0;
                hostDeadline = 0;
                return;
            }
        } catch (_) { /* A page may outlive its native channel during reload. */ }
        if (!state.visible || hostTimer) return;
        if (!hostDeadline) hostDeadline = Date.now() + 5000;
        hostTimer = setTimeout(function retry() {
            hostTimer = 0;
            if (!active) return;
            if (Date.now() >= hostDeadline) {
                close();
                inlineStatus('Settings are not ready yet. Please try again.');
                return;
            }
            report(pendingState);
        }, 100);
    }

    function inlineStatus(message) {
        const previous = document.getElementById(STATUS_ID);
        if (previous) previous.remove();
        const anchor = row && row.isConnected ? row : null;
        const content = anchor ? null : document.querySelector('#main,main,[role="main"]');
        if (!anchor && !rendered(content)) return;
        const status = document.createElement('div');
        status.id = STATUS_ID;
        status.setAttribute('data-whatly-settings-owned', 'status');
        status.setAttribute('role', 'status');
        status.style.cssText = 'padding:8px 24px;font:inherit;color:inherit;';
        status.textContent = message;
        if (anchor) anchor.after(status);
        else content.prepend(status);
        setTimeout(() => status.remove(), 6000);
    }

    function updatePanel() {
        if (!active || !context) return;
        const rect = contentRect();
        if (!rect) { close(); inlineStatus('There is not enough room to display Whatly Settings.'); return; }
        if (!placeholder) {
            placeholder = document.createElement('div');
            placeholder.id = PANEL_ID;
            placeholder.setAttribute('data-whatly-settings-owned', 'panel');
            placeholder.setAttribute('aria-hidden', 'true');
            placeholder.style.cssText = 'position:fixed;box-sizing:border-box;z-index:100;';
            document.body.appendChild(placeholder);
        }
        const colors = appearance(context.list);
        const styles = { left: rect.x + 'px', top: rect.y + 'px', width: rect.width + 'px', height: rect.height + 'px', backgroundColor: colors.background };
        for (const [key, value] of Object.entries(styles)) {
            if (placeholder.style[key] !== value) placeholder.style[key] = value;
        }
        report({ visible: true, ...rect, viewportWidth: innerWidth, viewportHeight: innerHeight, appearance: colors });
    }

    function openPanel() {
        const next = locate();
        if (!next) return false;
        clearTimeout(openTimer);
        openDeadline = 0;
        addRow(next);
        const status = document.getElementById(STATUS_ID);
        if (status) status.remove();
        active = true;
        paintRow();
        updatePanel();
        if (!activeTimer) activeTimer = setInterval(refresh, 1000);
        return active;
    }

    function close() {
        active = false;
        openDeadline = 0;
        hostDeadline = 0;
        clearTimeout(openTimer);
        clearTimeout(hostTimer);
        clearInterval(activeTimer);
        openTimer = hostTimer = activeTimer = 0;
        if (placeholder) placeholder.remove();
        placeholder = null;
        paintRow();
        report({ visible: false });
    }

    function open() {
        if (openPanel()) return true;
        const nav = railSettings();
        if (!nav) return false;
        close();
        nav.click();
        openDeadline = Date.now() + 4000;
        const attempt = function () {
            if (openPanel()) return;
            if (!openDeadline || Date.now() >= openDeadline) {
                openDeadline = 0;
                inlineStatus('Open WhatsApp Settings to use Whatly Settings.');
                return;
            }
            openTimer = setTimeout(attempt, 150);
        };
        openTimer = setTimeout(attempt, 100);
        return true;
    }

    function refresh() {
        scheduled = 0;
        if (active && (!context || !rendered(context.list) || !context.anchor.isConnected || !row || !row.isConnected)) close();
        const next = locate();
        if (!next) {
            if (active) close();
            if (row) row.remove();
            row = null;
            context = null;
            if (columnObserver) columnObserver.disconnect();
            return;
        }
        addRow(next);
        paintRow();
        updatePanel();
        if (!active) report({ visible: false });
    }

    function schedule() {
        if (!scheduled) scheduled = setTimeout(refresh, 120);
    }

    // Navigation is observed without reading messages or following React's
    // private state. Own mutations are ignored, and layout work is coalesced.
    const markerSelector = '[data-icon*="settings"],[data-icon="exit"],[data-icon*="logout"],[data-icon*="log-out"],[data-testid*="settings"]';
    const observer = new MutationObserver(records => {
        for (const record of records) {
            if (owned(record.target)) continue;
            if (context && (!context.list.isConnected || !context.anchor.isConnected || !row || !row.isConnected)) { schedule(); return; }
            for (const node of record.addedNodes) {
                if (!(node instanceof Element) || owned(node)) continue;
                if (node.matches(markerSelector) || node.querySelector(markerSelector)) { schedule(); return; }
            }
        }
    });
    observer.observe(document.documentElement, { childList: true, subtree: true });
    new MutationObserver(schedule).observe(document.documentElement, { attributes: true, attributeFilter: ['class', 'dir', 'data-theme', 'data-color-mode'] });
    document.addEventListener('click', event => {
        if (owned(event.target)) return;
        if (active && event.target.closest && event.target.closest(ACTIONS)) close();
        schedule();
    }, true);
    document.addEventListener('keydown', event => {
        if (!active || event.key !== 'Escape') return;
        event.preventDefault();
        event.stopPropagation();
        close();
        if (row && row.isConnected) row.focus();
    }, true);
    window.addEventListener('resize', schedule);
    window.addEventListener('popstate', close);
    window.addEventListener('hashchange', close);
    window.addEventListener('pagehide', close);
    document.addEventListener('visibilitychange', () => {
        if (document.hidden) close();
        else schedule();
    });

    window.__whatlySettingsNavigation = { open, close, refresh };
    refresh();
})();
