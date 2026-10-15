/* Settings data stays in this local page. WhatsApp never receives the bridge. */
(() => {
    "use strict";

    const root = document.documentElement;
    const content = document.getElementById("settings-content");
    const sectionsHost = document.getElementById("settings-sections");
    const search = document.getElementById("settings-search");
    const clearSearch = document.getElementById("clear-search");
    const searchSummary = document.getElementById("search-summary");
    const feedback = document.getElementById("feedback");
    const sectionNodes = new Map();
    const controlNodes = new Map();
    let bridge;
    let mutationQueue = Promise.resolve();
    let nextDomId = 0;

    const asText = value => value == null ? "" : String(value);

    function element(tag, className, text) {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined) node.textContent = asText(text);
        return node;
    }

    function showError(message) {
        document.getElementById("feedback-message").textContent = message || "That setting could not be changed. Please try again.";
        feedback.hidden = false;
    }

    // Qt supplies only appearance values, never markup or executable styles.
    window.applyAppearance = appearance => {
        if (!appearance || typeof appearance !== "object") return;
        if (appearance.mode === "dark" || appearance.mode === "light") {
            root.dataset.mode = appearance.mode;
        }
        for (const name of ["background", "foreground", "muted", "accent"]) {
            if (typeof appearance[name] === "string" && CSS.supports("color", appearance[name])) {
                root.style.setProperty(`--${name}`, appearance[name]);
            }
        }
        if (typeof appearance.fontFamily === "string" && CSS.supports("font-family", appearance.fontFamily)) {
            root.style.setProperty("--font-family", appearance.fontFamily);
        }
    };

    function call(method, ...args) {
        return new Promise((resolve, reject) => {
            if (!bridge || typeof bridge[method] !== "function") {
                reject(new Error("Settings are unavailable. Close this panel and try again."));
                return;
            }
            try {
                bridge[method](...args, resolve);
            } catch (error) {
                reject(error);
            }
        });
    }

    // A text field's change must reach Qt before its adjacent Apply action.
    function mutate(method, args, onSuccess) {
        mutationQueue = mutationQueue.then(async () => {
            const result = await call(method, ...args);
            if (!result || !result.ok) {
                showError(result && result.error);
                // Restore rejected toggles/selections while keeping text drafts.
                call("snapshot").then(render).catch(error => showError(error.message));
                return;
            }
            if (onSuccess) onSuccess();
        }).catch(error => showError(error.message));
    }

    function setValue(state, value) {
        const revision = state.revision;
        mutate("setValue", [state.id, value], () => {
            if (state.revision === revision) {
                state.dirty = false;
                if (state.type === "password" && state.input) state.input.value = "";
            }
        });
    }

    function createControl(control) {
        const row = element("div", "setting-row");
        row.dataset.controlId = control.id;
        row.dataset.type = control.type;
        const main = element("div", "row-main");
        const copy = element("div", "row-copy");
        const label = element("label", "setting-label");
        const description = element("p", "setting-description");
        const domId = `setting-${++nextDomId}`;
        label.id = `${domId}-label`;
        description.id = `${domId}-description`;
        copy.append(label, description);
        main.append(copy);
        row.append(main);

        const state = {
            id: control.id, type: control.type, row, main, copy, label, description,
            dirty: false, revision: 0, domId, options: new Map()
        };
        let input;

        switch (control.type) {
        case "toggle":
            input = element("input", "toggle");
            input.type = "checkbox";
            input.setAttribute("role", "switch");
            input.addEventListener("change", () => setValue(state, input.checked));
            main.append(input);
            break;
        case "select":
            input = element("select", "setting-select");
            input.addEventListener("change", () => {
                const selected = input.options[input.selectedIndex];
                if (selected) setValue(state, selected._bridgeValue);
            });
            main.append(input);
            break;
        case "text":
        case "password":
        case "shortcut":
        case "number":
        case "time": {
            input = element("input", "setting-input");
            input.type = ["password", "number", "time"].includes(control.type) ? control.type : "text";
            input.autocomplete = "off";
            input.spellcheck = false;
            const wrapper = element("div", "field-wrapper");
            state.prefix = element("span", "field-affix");
            state.suffix = element("span", "field-affix");
            wrapper.append(state.prefix, input, state.suffix);
            if (["number", "time"].includes(control.type)) main.append(wrapper);
            else row.append(wrapper);
            input.addEventListener("input", () => {
                state.dirty = true;
                state.revision++;
            });
            input.addEventListener("change", () => {
                if (!input.checkValidity()) {
                    input.reportValidity();
                    return;
                }
                const value = control.type === "number" ? input.valueAsNumber : input.value;
                if (typeof value === "number" && !Number.isFinite(value)) return;
                setValue(state, value);
            });
            input.addEventListener("keydown", event => {
                if (event.key === "Enter") {
                    event.preventDefault();
                    input.blur();
                }
            });
            break;
        }
        case "button":
            input = element("button", "action-button");
            input.type = "button";
            input.addEventListener("click", () => mutate("activate", [state.id]));
            copy.hidden = true;
            main.append(input);
            row.append(description);
            break;
        case "list":
        case "multiselect":
            state.list = element("div", "option-list");
            state.list.setAttribute("role", "group");
            state.list.setAttribute("aria-labelledby", label.id);
            state.empty = element("p", "list-empty", "No items to show");
            row.append(state.list, state.empty);
            break;
        default:
            state.value = element("p", "setting-value");
            row.append(state.value);
            break;
        }

        if (input) {
            input.id = domId;
            input.setAttribute("aria-describedby", description.id);
            if (control.type !== "button") {
                label.htmlFor = domId;
            }
            state.input = input;
        }
        return state;
    }

    function syncSelect(state, control) {
        const options = Array.isArray(control.options) ? control.options : [];
        const present = new Set();
        options.forEach((option, index) => {
            const key = asText(option.value);
            present.add(key);
            let node = state.options.get(key);
            if (!node) {
                node = document.createElement("option");
                state.options.set(key, node);
            }
            node.value = key;
            node._bridgeValue = option.value;
            node.textContent = asText(option.label);
            node.disabled = option.enabled === false;
            if (state.input.children[index] !== node) {
                state.input.insertBefore(node, state.input.children[index] || null);
            }
        });
        for (const [key, node] of state.options) {
            if (!present.has(key)) {
                node.remove();
                state.options.delete(key);
            }
        }
        state.input.value = asText(control.value);
    }

    function syncList(state, control) {
        const options = Array.isArray(control.options) ? control.options : [];
        const present = new Set();
        options.forEach((option, index) => {
            const key = asText(option.value);
            present.add(key);
            let item = state.options.get(key);
            const checkable = control.type === "multiselect" || option.checkable;
            if (!item) {
                const row = element("div", "option-row");
                const label = element("label", "option-label");
                const input = element("input", "option-input");
                input.name = state.domId;
                const copy = element("span");
                const text = element("span");
                const description = element("span", "option-description");
                const progress = element("progress", "option-progress");
                progress.max = 100;
                copy.append(text, description, progress);
                label.append(input, copy);
                const action = element("button", "action-button");
                action.type = "button";
                row.append(label, action);
                item = { row, label, input, text, description, progress, action };
                state.options.set(key, item);
                input.addEventListener("change", () => {
                    const current = item.definition;
                    const value = input.type === "checkbox"
                        ? { index: current.value, checked: input.checked }
                        : current.value;
                    setValue(state, value);
                });
                action.addEventListener("click", () => mutate("activate", [item.definition.actionId]));
            }
            item.definition = option;
            item.input.type = checkable ? "checkbox" : "radio";
            item.input.checked = checkable ? Boolean(option.checked) : option.value === control.value;
            item.input.disabled = control.enabled === false || control.readOnly || option.enabled === false;
            item.text.textContent = asText(option.label);
            item.description.textContent = asText(option.description);
            item.description.hidden = !option.description;
            item.action.textContent = asText(option.actionLabel || "Manage");
            item.action.hidden = !option.actionId;
            item.action.disabled = control.enabled === false || control.readOnly || option.actionEnabled === false;
            item.action.setAttribute("aria-label", `${option.actionLabel || "Manage"} ${option.label}`);
            item.progress.hidden = typeof option.progress !== "number" || option.progress < 0;
            if (!item.progress.hidden) item.progress.value = option.progress;
            if (state.list.children[index] !== item.row) {
                state.list.insertBefore(item.row, state.list.children[index] || null);
            }
        });
        for (const [key, item] of state.options) {
            if (!present.has(key)) {
                item.row.remove();
                state.options.delete(key);
            }
        }
        state.list.hidden = options.length === 0;
        state.empty.hidden = options.length !== 0;
    }

    function updateControl(state, control, sectionTitle) {
        state.definition = control;
        state.label.textContent = asText(control.label);
        state.description.textContent = asText(control.description);
        state.description.hidden = !control.description;
        state.searchText = [sectionTitle, control.label, control.description].map(asText).join(" ").toLocaleLowerCase();
        const input = state.input;
        if (input) {
            input.disabled = control.enabled === false || (control.readOnly && !["text", "password", "shortcut", "number", "time"].includes(control.type));
            input.readOnly = Boolean(control.readOnly);
        }
        switch (control.type) {
        case "toggle":
            input.checked = Boolean(control.value);
            break;
        case "select":
            syncSelect(state, control);
            break;
        case "text":
        case "password":
        case "shortcut":
        case "number":
        case "time":
            // Updating unrelated settings must never discard an unfinished edit.
            if (!state.dirty && document.activeElement !== input) {
                input.value = control.type === "password" ? "" : asText(control.value);
            }
            input.placeholder = control.type === "password" && control.configured
                ? "Already set · leave blank to keep"
                : asText(control.placeholder);
            for (const [attribute, property] of [["min", "minimum"], ["max", "maximum"], ["step", "step"]]) {
                if (control[property] !== undefined) input.setAttribute(attribute, control[property]);
                else input.removeAttribute(attribute);
            }
            state.prefix.textContent = asText(control.prefix);
            state.prefix.hidden = !control.prefix;
            state.suffix.textContent = asText(control.suffix);
            state.suffix.hidden = !control.suffix;
            break;
        case "button":
            input.textContent = asText(control.label);
            input.dataset.destructive = control.destructive ? "true" : "false";
            break;
        case "list":
        case "multiselect":
            syncList(state, control);
            break;
        default:
            state.value.textContent = asText(control.value);
            state.value.hidden = !asText(control.value);
        }
    }

    function filterSettings() {
        const words = search.value.trim().toLocaleLowerCase().split(/\s+/).filter(Boolean);
        let visible = 0;
        for (const section of sectionNodes.values()) {
            let sectionVisible = 0;
            for (const state of section.controls) {
                const matches = words.every(word => state.searchText.includes(word));
                state.row.hidden = !matches;
                if (matches) sectionVisible++;
            }
            section.node.hidden = sectionVisible === 0;
            visible += sectionVisible;
        }
        clearSearch.hidden = words.length === 0;
        searchSummary.hidden = words.length === 0;
        searchSummary.textContent = `${visible} ${visible === 1 ? "setting" : "settings"} found`;
        document.getElementById("no-results").hidden = visible !== 0 || words.length === 0;
    }

    function render(snapshot) {
        if (!snapshot || !Array.isArray(snapshot.sections)) {
            document.getElementById("loading-settings").hidden = true;
            content.setAttribute("aria-busy", "false");
            showError("Settings could not be loaded. Close this panel and try again.");
            return;
        }
        const scrollTop = content.scrollTop;
        const presentSections = new Set();
        const presentControls = new Set();
        document.getElementById("panel-title").textContent = snapshot.title || "Whatly Settings";
        document.title = snapshot.title || "Whatly Settings";
        snapshot.sections.forEach((definition, index) => {
            const key = asText(definition.id);
            presentSections.add(key);
            let section = sectionNodes.get(key);
            if (!section) {
                const node = element("section", "settings-section");
                const heading = element("h2", "section-heading");
                heading.id = `section-${++nextDomId}`;
                node.setAttribute("aria-labelledby", heading.id);
                const rows = element("div", "section-controls");
                node.append(heading, rows);
                section = { node, heading, rows, controls: [] };
                sectionNodes.set(key, section);
            }
            section.heading.textContent = asText(definition.title);
            section.controls = [];
            (definition.controls || []).forEach((control, controlIndex) => {
                presentControls.add(control.id);
                let state = controlNodes.get(control.id);
                if (state && state.type !== control.type) {
                    state.row.remove();
                    controlNodes.delete(control.id);
                    state = null;
                }
                if (!state) {
                    state = createControl(control);
                    controlNodes.set(control.id, state);
                }
                updateControl(state, control, definition.title);
                section.controls.push(state);
                if (section.rows.children[controlIndex] !== state.row) {
                    section.rows.insertBefore(state.row, section.rows.children[controlIndex] || null);
                }
            });
            if (sectionsHost.children[index] !== section.node) {
                sectionsHost.insertBefore(section.node, sectionsHost.children[index] || null);
            }
        });
        for (const [key, state] of controlNodes) {
            if (!presentControls.has(key)) {
                state.row.remove();
                controlNodes.delete(key);
            }
        }
        for (const [key, section] of sectionNodes) {
            if (!presentSections.has(key)) {
                section.node.remove();
                sectionNodes.delete(key);
            }
        }
        document.getElementById("loading-settings").hidden = true;
        content.setAttribute("aria-busy", "false");
        filterSettings();
        content.scrollTop = scrollTop;
    }

    function close() {
        if (bridge && typeof bridge.close === "function") bridge.close();
    }

    function connect(settingsBridge) {
        bridge = settingsBridge;
        if (bridge && bridge.changed && typeof bridge.changed.connect === "function") {
            bridge.changed.connect(render);
        }
        call("snapshot").then(render).catch(error => {
            document.getElementById("loading-settings").hidden = true;
            content.setAttribute("aria-busy", "false");
            showError(error.message);
        });
    }

    document.getElementById("close-settings").addEventListener("click", close);
    document.getElementById("dismiss-feedback").addEventListener("click", () => { feedback.hidden = true; });
    search.addEventListener("input", filterSettings);
    clearSearch.addEventListener("click", () => {
        search.value = "";
        filterSettings();
        search.focus();
    });
    document.addEventListener("keydown", event => {
        if (event.key !== "Escape" || event.defaultPrevented || event.target.tagName === "SELECT") return;
        event.preventDefault();
        if (search.value) {
            search.value = "";
            filterSettings();
            search.focus();
        } else close();
    });

    // A fixture may provide the same interface without Qt; production uses QWebChannel.
    if (window.whatlySettingsBridge) connect(window.whatlySettingsBridge);
    else if (window.qt && window.qt.webChannelTransport && typeof QWebChannel === "function") {
        new QWebChannel(window.qt.webChannelTransport, channel => connect(channel.objects.settingsBridge));
    } else {
        document.getElementById("loading-settings").hidden = true;
        content.setAttribute("aria-busy", "false");
        showError("Settings are unavailable. Close this panel and try again.");
    }
})();
