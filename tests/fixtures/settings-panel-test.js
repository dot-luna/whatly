/* Browser regression fixture; no real account, credentials, or Qt transport.
 * Serve the repository root, then open tests/fixtures/settings-panel.html.
 * Results are available in window.testResults and #test-results as JSON;
 * window.testDone becomes true after all asynchronous checks finish.
 * Add ?preview&theme=dark for an interactive visual fixture without test edits.
 */

const control = (id, type, label, value, extra = {}) => ({ id, type, label, value, ...extra });
window.fixtureCalls = [];
window.testDone = false;
window.testResults = [];
window.fixtureSnapshot = {
    title: "Whatly Settings",
    sections: [
        {
            id: "appearance", title: "Appearance", controls: [
                control("follow", "toggle", "Follow system theme", true, {
                    description: "Switch between light and dark with your desktop."
                }),
                control("theme", "select", "Theme", 0, {
                    options: [{ label: "Dark", value: 0 }, { label: "Light", value: 1 }]
                }),
                control("font", "text", "Chat font", "Inter", {
                    description: "Choose the typeface used in your conversations."
                }),
                control("zoom", "number", "Window zoom", 1, { minimum: 0.25, maximum: 5, step: 0.05 })
            ]
        },
        {
            id: "general", title: "General", controls: [
                control("tray", "toggle", "Minimize to tray", true, {
                    description: "Keep WhatsApp running when you close its window."
                }),
                control("mute", "toggle", "Mute audio", false),
                control("quiet", "time", "Quiet hours begin", "22:00"),
                control("shortcut", "shortcut", "Show window shortcut", "Ctrl+Alt+W")
            ]
        },
        {
            id: "advanced", title: "Advanced", controls: [
                control("ua", "text", "User agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36", {
                    description: "The browser identity WhatsApp sees."
                }),
                control("apply", "button", "Apply user agent"),
                control("secret", "password", "API token", "", { configured: true }),
                control("disabled", "toggle", "Disabled control", true, { enabled: false }),
                control("dict", "multiselect", "Spelling languages", undefined, {
                    options: [
                        { label: "English", value: 0, checked: true, actionId: "remove", actionLabel: "Remove" },
                        { label: "Spanish", value: 1, checked: false, actionId: "download", actionLabel: "Download" }
                    ]
                }),
                control("list", "list", "Saved responses", 0, {
                    options: [{ label: "Hello", value: 0 }, { label: "Thank you", value: 1 }]
                }),
                control("info", "info", "Storage location", "/home/person/.local/share/whatly")
            ]
        }
    ]
};

window.whatlySettingsBridge = {
    changed: { connect(fn) { window.fixtureEmit = fn; } },
    snapshot(callback) { callback(window.fixtureSnapshot); },
    setValue(id, value, callback) {
        window.fixtureCalls.push(["setValue", id, value]);
        // Delay acknowledges so an adjacent Apply click exercises queue ordering.
        setTimeout(() => callback({ ok: true }), 10);
    },
    activate(id, callback) {
        window.fixtureCalls.push(["activate", id]);
        callback(id === "fail" ? { ok: false, error: "Could not apply this setting." } : { ok: true });
    },
    close() { window.fixtureCalls.push(["close"]); }
};

window.addEventListener("load", async () => {
    const parameters = new URLSearchParams(location.search);
    if (parameters.has("preview")) {
        window.applyAppearance({ mode: parameters.get("theme") === "dark" ? "dark" : "light" });
        return;
    }
    const results = window.testResults;
    window.fixtureResults = results;
    const assert = (name, value) => {
        results.push({ name, passed: Boolean(value) });
        if (!value) throw new Error(name);
    };
    const equal = (left, right) => JSON.stringify(left) === JSON.stringify(right);
    const row = id => document.querySelector(`[data-control-id="${id}"]`);
    const field = id => row(id).querySelector("input,select,button");
    const change = node => node.dispatchEvent(new Event("change", { bubbles: true }));
    const tick = () => new Promise(resolve => setTimeout(resolve, 50));
    const visiblePanels = () => Array.from(document.querySelectorAll('[role="tabpanel"]')).filter(panel => !panel.hidden);
    const tab = id => document.querySelector(`[role="tab"][data-category-id="${id}"]`);
    const chooseCategory = id => {
        document.getElementById("category-select").value = id;
        change(document.getElementById("category-select"));
    };

    try {
        assert("all types render", document.querySelectorAll("[data-control-id]").length === 15);
        assert("loading finishes", document.querySelector("main").getAttribute("aria-busy") === "false");
        assert("passwords never prefilled", field("secret").value === "" && field("secret").placeholder.includes("Already set"));
        assert("disabled controls disabled", field("disabled").disabled);
        assert("categories have accessible tabs and associated panels", document.querySelectorAll('[role="tab"]').length === 3 &&
            document.getElementById(tab("appearance").getAttribute("aria-controls")).getAttribute("aria-labelledby") === tab("appearance").id);
        assert("only the selected category is visible", visiblePanels().length === 1 && visiblePanels()[0].dataset.categoryId === "appearance" && row("tray").hidden);
        chooseCategory("general");
        assert("category selector changes the visible form", visiblePanels().length === 1 && !row("tray").hidden && row("font").hidden && tab("general").getAttribute("aria-selected") === "true");
        fixtureEmit(fixtureSnapshot);
        assert("backend refresh retains the selected category", visiblePanels()[0].dataset.categoryId === "general");
        if (matchMedia("(min-width: 620px)").matches) {
            tab("general").focus();
            tab("general").dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowDown", bubbles: true, cancelable: true }));
            assert("category navigation is keyboard accessible", document.activeElement === tab("advanced") && visiblePanels()[0].dataset.categoryId === "advanced");
        } else {
            assert("category navigation is keyboard accessible", getComputedStyle(document.querySelector(".compact-category")).display !== "none" &&
                document.getElementById("category-select").getAttribute("aria-label") === "Settings category");
        }
        chooseCategory("appearance");

        field("follow").checked = false;
        change(field("follow"));
        await tick();
        assert("toggle passes boolean", equal(fixtureCalls.at(-1), ["setValue", "follow", false]));
        field("theme").value = "1";
        change(field("theme"));
        await tick();
        assert("select passes numeric index", equal(fixtureCalls.at(-1), ["setValue", "theme", 1]));
        field("zoom").value = "1.25";
        change(field("zoom"));
        await tick();
        assert("number passes number", equal(fixtureCalls.at(-1), ["setValue", "zoom", 1.25]));

        // Backend refreshes must preserve an edit, its caret, and the current page position.
        const input = field("font");
        input.focus();
        input.value = "Draft font";
        input.dispatchEvent(new Event("input"));
        input.setSelectionRange(2, 5);
        document.querySelector("main").style.maxHeight = "180px";
        document.querySelector("main").scrollTop = 90;
        const scroll = document.querySelector("main").scrollTop;
        fixtureSnapshot.sections[0].controls[2].value = "Changed elsewhere";
        fixtureEmit(fixtureSnapshot);
        assert("snapshot preserves dirty value", input.value === "Draft font");
        assert("snapshot preserves focus and selection", document.activeElement === input && input.selectionStart === 2 && input.selectionEnd === 5);
        assert("snapshot preserves scroll", scroll > 0 && document.querySelector("main").scrollTop === scroll);
        chooseCategory("general");
        chooseCategory("appearance");
        assert("switching categories preserves drafts and control identity", field("font") === input && input.value === "Draft font");
        assert("returning to a category restores its scroll position", document.querySelector("main").scrollTop === scroll);
        document.querySelector("main").style.maxHeight = "";
        change(input);
        chooseCategory("advanced");
        field("apply").click();
        await tick();
        assert("text save precedes apply", equal(fixtureCalls.slice(-2), [
            ["setValue", "font", "Draft font"], ["activate", "apply"]
        ]));

        const search = document.getElementById("settings-search");
        search.value = "window zoom";
        search.dispatchEvent(new Event("input"));
        assert("search filters all words", !row("zoom").hidden && row("font").hidden);
        assert("global search jumps to a matching category", visiblePanels().length === 1 && visiblePanels()[0].dataset.categoryId === "appearance");
        search.value = "window";
        search.dispatchEvent(new Event("input"));
        assert("search shows category match counts", !tab("appearance").hidden && !tab("general").hidden && tab("advanced").hidden &&
            !tab("general").querySelector(".category-count").hidden && document.getElementById("search-summary").textContent.includes("2 categories"));
        chooseCategory("general");
        assert("search still shows only one selected category", visiblePanels().length === 1 && !row("tray").hidden && row("zoom").hidden);
        search.value = "unfindable";
        search.dispatchEvent(new Event("input"));
        assert("empty search result visible", !document.getElementById("no-results").hidden && visiblePanels().length === 0 && document.getElementById("category-select").disabled);
        document.getElementById("clear-search").click();
        assert("clear search restores the previous category", !row("ua").hidden && row("font").hidden && document.getElementById("no-results").hidden && visiblePanels().length === 1);

        const checkbox = row("dict").querySelectorAll("input")[1];
        checkbox.checked = true;
        change(checkbox);
        await tick();
        assert("dictionary toggle carries row and checked", equal(fixtureCalls.at(-1), ["setValue", "dict", { index: 1, checked: true }]));
        row("dict").querySelectorAll("button")[1].click();
        await tick();
        assert("dictionary action dispatched", equal(fixtureCalls.at(-1), ["activate", "download"]));
        const radio = row("list").querySelectorAll("input")[1];
        radio.checked = true;
        change(radio);
        await tick();
        assert("list selection carries index", equal(fixtureCalls.at(-1), ["setValue", "list", 1]));

        fixtureSnapshot.sections[0].controls[0].description = "<img src=x onerror=alert(1)>";
        fixtureSnapshot.sections[2].controls.push(control("fail", "button", "Failure fixture"));
        fixtureEmit(fixtureSnapshot);
        assert("markup remains plain text", row("follow").querySelector("img") === null && row("follow").textContent.includes("<img"));
        field("fail").click();
        await tick();
        assert("mutation errors visible", !document.getElementById("feedback").hidden && document.getElementById("feedback-message").textContent.includes("Could not apply"));
        document.getElementById("dismiss-feedback").click();
        assert("errors dismissible", document.getElementById("feedback").hidden);

        window.applyAppearance({ mode: "dark", background: "#111b21", foreground: "#e9edef", accent: "#00a884", fontFamily: "Arial" });
        assert("appearance follows supplied palette", getComputedStyle(document.body).backgroundColor === "rgb(17, 27, 33)");
        assert("no horizontal document overflow", document.documentElement.scrollWidth === window.innerWidth);
        assert("category layout adapts to available width", matchMedia("(min-width: 620px)").matches
            ? getComputedStyle(document.getElementById("category-sidebar")).display !== "none" && getComputedStyle(document.querySelector(".compact-category")).display === "none"
            : getComputedStyle(document.getElementById("category-sidebar")).display === "none" && getComputedStyle(document.querySelector(".compact-category")).display !== "none");
        const removed = fixtureSnapshot.sections.splice(2, 1)[0];
        fixtureEmit(fixtureSnapshot);
        assert("removed selected category falls back to an available one", visiblePanels().length === 1 && visiblePanels()[0].dataset.categoryId === "appearance" && !tab("advanced"));
        fixtureSnapshot.sections.push(removed);
        fixtureEmit(fixtureSnapshot);
        document.getElementById("close-settings").click();
        assert("back closes panel", fixtureCalls.at(-1)[0] === "close");
    } catch (error) {
        window.testError = error.message;
        results.push({ error: error.message });
    }

    const result = document.createElement("pre");
    result.id = "test-results";
    result.textContent = JSON.stringify(results);
    result.hidden = true;
    document.body.append(result);
    document.querySelector("main").scrollTop = 0;
    window.testDone = true;
});
