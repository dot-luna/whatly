// Exercise the real settings handlers, bundled WebEngine form, and synthetic
// navigation fixtures without touching personal settings or loading WhatsApp.
#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimeEdit>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>

#include "dictionaryrows.h"
#include "embeddedsettingsbridge.h"
#include "embeddedsettingsview.h"
#include "mainwindow.h"
#include "pagebridge.h"
#include "settingsmanager.h"
#include "settingswidget.h"
#include "setupwizard.h"
#include "updatechecker.h"
#include "webengineprofilemanager.h"
#include "webview.h"

namespace {
class BlockRemoteRequests : public QWebEngineUrlRequestInterceptor {
public:
  void interceptRequest(QWebEngineUrlRequestInfo &request) override {
    const QString scheme = request.requestUrl().scheme();
    request.block(scheme == QLatin1String("http") || scheme == QLatin1String("https"));
  }
};
} // namespace

class TstEmbeddedSettingsBridge : public QObject {
  Q_OBJECT
  QTemporaryDir m_settingsDir;
  QTimer m_dialogCloser;

  static QVariantMap control(const QVariantMap &state, const QString &id) {
    for (const QVariant &section : state.value(QStringLiteral("sections")).toList())
      for (const QVariant &entry : section.toMap().value(QStringLiteral("controls")).toList())
        if (entry.toMap().value(QStringLiteral("id")).toString() == id)
          return entry.toMap();
    return {};
  }

  static bool ok(const QVariantMap &response) {
    return response.value(QStringLiteral("ok")).toBool();
  }

  static QVariant javascript(QWebEnginePage *page, const QString &code) {
    QVariant value;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    // The context guard prevents a late renderer callback from accessing this
    // stack after a timeout has ended the wait.
    QPointer<QEventLoop> context(&loop);
    page->runJavaScript(code, [&value, context](const QVariant &result) {
      if (context) {
        value = result;
        context->quit();
      }
    });
    timeout.start(5000);
    loop.exec();
    return value;
  }

private slots:
  void initTestCase() {
    QVERIFY(m_settingsDir.isValid());
    qputenv("WHATLY_SETTINGS_APP", "whatly-embedded-settings-test");
    QCoreApplication::setOrganizationName(QStringLiteral("shakaran"));
    QCoreApplication::setApplicationName(QStringLiteral("whatly-embedded-settings-test"));
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_settingsDir.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    m_dialogCloser.setInterval(10);
    connect(&m_dialogCloser, &QTimer::timeout, [] {
      for (QWidget *widget : QApplication::topLevelWidgets())
        if (auto *dialog = qobject_cast<QDialog *>(widget))
          if (dialog->isVisible())
            dialog->reject();
    });
    m_dialogCloser.start();
  }

  void init() {
    QSettings &saved = SettingsManager::instance().settings();
    saved.clear();
    // Set before the first profile-manager access, including the standalone
    // form's setting handlers: Chromium must never use a personal data folder.
    saved.setValue(QStringLiteral("storage/dataDir"), m_settingsDir.path() + QStringLiteral("/browser"));
  }

  void exportsAllSectionsWhileNativeWindowStaysHidden() {
    QTemporaryDir cache, storage;
    SettingsWidget settings(nullptr, 0, cache.path(), storage.path());
    EmbeddedSettingsBridge bridge(&settings);
    const QVariantMap state = bridge.snapshot();
    QCOMPARE(state.value(QStringLiteral("title")).toString(), QStringLiteral("Whatly Settings"));
    QCOMPARE(state.value(QStringLiteral("sections")).toList().size(), 16);
    for (const char *id : {"languageComboBox", "fontFamilyComboBox", "notificationCheckBox",
                           "spellCheckLanguageComboBox", "aiApiKeyLineEdit", "applock_checkbox",
                           "zoomFactorSpinBox", "cacheSize", "disableGpuCheckBox",
                           "proxyHostLineEdit", "jsAddonsList", "cannedList",
                           "cloudPhoneIdEdit", "localApiPortSpinBox", "userAgentLineEdit",
                           "collectDiagnosticsCheckBox", "copyDiagnosticsButton"}) {
      const QVariantMap entry = control(state, QLatin1String(id));
      QVERIFY2(!entry.isEmpty(), id);
      QVERIFY2(!entry.value(QStringLiteral("label")).toString().isEmpty(), id);
    }
    QVERIFY(control(state, QStringLiteral("settingsSearchBox")).isEmpty());
    QVERIFY(control(state, QStringLiteral("qt_spinbox_lineedit")).isEmpty());
    QVERIFY(control(state, QStringLiteral("viewPassword")).isEmpty());
    QVERIFY(!settings.isVisible());
  }

  void toggleUsesExistingSignalAndPersistence() {
    QTemporaryDir cache, storage;
    SettingsWidget settings(nullptr, 0, cache.path(), storage.path());
    EmbeddedSettingsBridge bridge(&settings);
    QSignalSpy mute(&settings, &SettingsWidget::muteToggled);
    QSignalSpy changed(&bridge, &EmbeddedSettingsBridge::changed);
    auto *checkbox = settings.findChild<QCheckBox *>(QStringLiteral("muteAudioCheckBox"));
    QVERIFY(checkbox);
    const bool next = !checkbox->isChecked();
    QVERIFY(ok(bridge.setValue(QStringLiteral("muteAudioCheckBox"), next)));
    QCOMPARE(checkbox->isChecked(), next);
    QCOMPARE(SettingsManager::instance().settings().value(QStringLiteral("muteAudio")).toBool(), next);
    QCOMPARE(mute.count(), 1);
    QVERIFY(changed.count() >= 1);
    QVERIFY(!settings.isVisible());
  }

  void userAgentIsSavedOnlyByItsExistingApplyAction() {
    QTemporaryDir cache, storage;
    SettingsWidget settings(nullptr, 0, cache.path(), storage.path());
    EmbeddedSettingsBridge bridge(&settings);
    QSignalSpy changed(&settings, &SettingsWidget::userAgentChanged);
    const QString agent = QStringLiteral("Mozilla/5.0 Chrome/130.0.0.0 test");
    QVERIFY(ok(bridge.setValue(QStringLiteral("userAgentLineEdit"), agent)));
    QCOMPARE(changed.count(), 0);
    QVERIFY(ok(bridge.activate(QStringLiteral("setUserAgent"))));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().first().toString(), agent);
    // MainWindow receives this signal and updates every profile plus storage;
    // the standalone settings widget does not own that application-wide work.
  }

  void secretsStayRedactedAndAbsentWritesNeverClearThem() {
    QTemporaryDir cache, storage;
    SettingsWidget settings(nullptr, 0, cache.path(), storage.path());
    EmbeddedSettingsBridge bridge(&settings);
    const QString secret = QStringLiteral("secret-never-exposed-by-snapshot");
    for (const char *name : {"current_password", "cloudTokenEdit", "aiApiKeyLineEdit",
                             "translateApiKeyLineEdit", "proxyPasswordLineEdit",
                             "localApiTokenEdit", "webhookVerifyTokenEdit", "webhookAppSecretEdit"}) {
      auto *edit = settings.findChild<QLineEdit *>(QLatin1String(name));
      QVERIFY(edit);
      edit->setText(secret);
      // Even the native reveal state must not export the password.
      edit->setEchoMode(QLineEdit::Normal);
      const QVariantMap entry = control(bridge.snapshot(), QLatin1String(name));
      QCOMPARE(entry.value(QStringLiteral("type")).toString(), QStringLiteral("password"));
      QVERIFY(entry.value(QStringLiteral("value")).toString().isEmpty());
      QVERIFY(entry.value(QStringLiteral("configured")).toBool());
      QVERIFY(!ok(bridge.setValue(QLatin1String(name), QVariant())));
      QCOMPARE(edit->text(), secret);
    }
    QVERIFY(!QJsonDocument::fromVariant(bridge.snapshot()).toJson().contains(secret.toUtf8()));
    QVERIFY(ok(bridge.setValue(QStringLiteral("aiApiKeyLineEdit"), QStringLiteral("replacement"))));
    QCOMPARE(settings.findChild<QLineEdit *>(QStringLiteral("aiApiKeyLineEdit"))->text(), QStringLiteral("replacement"));
  }

  void rejectsUnknownMalformedReadOnlyAndDisabledOperations() {
    QTemporaryDir cache, storage;
    SettingsWidget settings(nullptr, 0, cache.path(), storage.path());
    EmbeddedSettingsBridge bridge(&settings);
    QVERIFY(!ok(bridge.setValue(QStringLiteral("windowTheme"), QStringLiteral("dark"))));
    QVERIFY(!ok(bridge.activate(QStringLiteral("clearAllData"))));
    QVERIFY(!ok(bridge.activate(QStringLiteral("viewPassword"))));
    QVERIFY(!ok(bridge.setValue(QStringLiteral("muteAudioCheckBox"), QStringLiteral("false"))));
    QVERIFY(!ok(bridge.setValue(QStringLiteral("languageComboBox"), -1)));
    QVERIFY(!ok(bridge.setValue(QStringLiteral("languageComboBox"), 0.5)));
    QVERIFY(!ok(bridge.setValue(QStringLiteral("interfaceFontSizeSpinBox"), 999999)));
    QVERIFY(!ok(bridge.setValue(QStringLiteral("defaultDownloadLocation"), QStringLiteral("/tmp"))));
    QVERIFY(!ok(bridge.setValue(QStringLiteral("zoomFactorSpinBox"), 2.0)));
    QVERIFY(!ok(bridge.activate(QStringLiteral("dictionary/-1"))));
    QVERIFY(!ok(bridge.activate(QStringLiteral("dictionary/0suffix"))));

    QVERIFY(ok(bridge.setValue(QStringLiteral("followSystemThemeCheckBox"), true)));
    QVERIFY(!control(bridge.snapshot(), QStringLiteral("themeComboBox")).value(QStringLiteral("enabled")).toBool());
    QVERIFY(!ok(bridge.setValue(QStringLiteral("themeComboBox"), 0)));
  }

  void timeAndListSelectionUseActualControls() {
    QTemporaryDir cache, storage;
    SettingsWidget settings(nullptr, 0, cache.path(), storage.path());
    EmbeddedSettingsBridge bridge(&settings);
    QVERIFY(ok(bridge.setValue(QStringLiteral("dndCheckBox"), true)));
    QVERIFY(ok(bridge.setValue(QStringLiteral("dndStartTimeEdit"), QStringLiteral("22:45"))));
    QCOMPARE(settings.findChild<QTimeEdit *>(QStringLiteral("dndStartTimeEdit"))->time(), QTime(22, 45));
    QVERIFY(!ok(bridge.setValue(QStringLiteral("dndStartTimeEdit"), QStringLiteral("25:00"))));
    auto *list = settings.findChild<QListWidget *>(QStringLiteral("cannedList"));
    QVERIFY(list);
    list->addItem(QStringLiteral("Bridge test reply"));
    QVERIFY(ok(bridge.setValue(QStringLiteral("cannedList"), list->count() - 1)));
    QCOMPARE(list->currentItem()->text(), QStringLiteral("Bridge test reply"));
    QVERIFY(!ok(bridge.setValue(QStringLiteral("cannedList"), QVariantMap{{QStringLiteral("index"), list->currentRow()}, {QStringLiteral("checked"), true}})));
  }

  void dictionaryListKeepsCheckboxAndActionSemantics() {
    QTemporaryDir cache, storage;
    SettingsWidget settings(nullptr, 0, cache.path(), storage.path());
    EmbeddedSettingsBridge bridge(&settings);
    auto *combo = settings.findChild<QComboBox *>(QStringLiteral("spellCheckLanguageComboBox"));
    QVERIFY(combo);
    auto *model = qobject_cast<QStandardItemModel *>(combo->model());
    QVERIFY(model);
    auto *item = new QStandardItem(QStringLiteral("Test dictionary"));
    item->setData(QStringLiteral("zz_TEST"), DictionaryRows::CodeRole);
    item->setData(true, DictionaryRows::InstalledRole);
    item->setData(static_cast<int>(DictionaryRows::Action::None), DictionaryRows::ActionRole);
    item->setData(DictionaryRows::Idle, DictionaryRows::ProgressRole);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
    model->appendRow(item);
    combo->setEnabled(true);
    const int row = model->rowCount() - 1;
    QVERIFY(ok(bridge.setValue(combo->objectName(), QVariantMap{{QStringLiteral("index"), row}, {QStringLiteral("checked"), true}})));
    QCOMPARE(item->checkState(), Qt::Checked);
    const QVariantMap entry = control(bridge.snapshot(), combo->objectName());
    QCOMPARE(entry.value(QStringLiteral("type")).toString(), QStringLiteral("multiselect"));
    QVERIFY(!ok(bridge.activate(QStringLiteral("dictionary/") + QString::number(row))));
  }

  void closeRequestsEmbeddedPanelDismissalOnly() {
    QTemporaryDir cache, storage;
    SettingsWidget settings(nullptr, 0, cache.path(), storage.path());
    EmbeddedSettingsBridge bridge(&settings);
    QSignalSpy close(&bridge, &EmbeddedSettingsBridge::closeRequested);
    bridge.close();
    QCOMPARE(close.count(), 1);
    QVERIFY(!settings.isVisible());
  }

  void bundledPageEditsRealSettingsThroughPrivateChannel() {
    QTemporaryDir cache, storage;
    SettingsWidget settings(nullptr, 0, cache.path(), storage.path());
    EmbeddedSettingsView view(&settings, nullptr);
    view.resize(760, 850);
    QSignalSpy loaded(&view, &QWebEngineView::loadFinished);
    view.show();
    QVERIFY(loaded.wait(20000));
    QVERIFY(loaded.last().first().toBool());
    QCOMPARE(view.url(), QUrl(QStringLiteral("qrc:/whatly/settings/settings.html")));
    QVERIFY(view.page()->profile()->isOffTheRecord());
    QTRY_VERIFY_WITH_TIMEOUT(javascript(view.page(), QStringLiteral(
        "!!document.querySelector('[data-control-id=\"smoothScrollingCheckBox\"] input')")).toBool(), 20000);
    javascript(view.page(), QStringLiteral(
        "(() => { const search = document.getElementById('settings-search');"
        "search.value='smooth scrolling'; search.dispatchEvent(new Event('input',{bubbles:true})); })();"));
    QTRY_VERIFY(javascript(view.page(), QStringLiteral(
        "document.querySelector('[data-control-id=\"smoothScrollingCheckBox\"] input').getBoundingClientRect().height > 0")).toBool());
    view.setAppearance({{QStringLiteral("background"), QStringLiteral("rgb(20, 31, 42)")},
                        {QStringLiteral("foreground"), QStringLiteral("rgba(230, 235, 240, 0.9)")},
                        {QStringLiteral("mode"), QStringLiteral("dark")}});
    QTRY_COMPARE(javascript(view.page(), QStringLiteral(
        "getComputedStyle(document.body).backgroundColor")).toString(), QStringLiteral("rgb(20, 31, 42)"));
    QCOMPARE(javascript(view.page(), QStringLiteral(
        "getComputedStyle(document.body).color")).toString(), QStringLiteral("rgba(230, 235, 240, 0.9)"));
    auto *checkbox = settings.findChild<QCheckBox *>(QStringLiteral("smoothScrollingCheckBox"));
    QVERIFY(checkbox);
    const bool next = !checkbox->isChecked();
    QVERIFY(javascript(view.page(), QStringLiteral(
        "(() => { const input = document.querySelector('[data-control-id=\"smoothScrollingCheckBox\"] input');"
        "input.checked = %1; input.dispatchEvent(new Event('change', {bubbles:true})); return true; })()")
        .arg(next ? QStringLiteral("true") : QStringLiteral("false"))).toBool());
    QTRY_COMPARE(checkbox->isChecked(), next);
    QCOMPARE(SettingsManager::instance().settings().value(QStringLiteral("smoothScrolling")).toBool(), next);
    QVERIFY(!settings.isVisible());

    // The ordinary WhatsApp bridge has no settings-read or settings-write API.
    PageBridge pageBridge;
    QCOMPARE(pageBridge.metaObject()->indexOfSlot("snapshot()"), -1);
    QCOMPARE(pageBridge.metaObject()->indexOfSlot("setValue(QString,QVariant)"), -1);
    QCOMPARE(pageBridge.metaObject()->indexOfSlot("activate(QString)"), -1);

    QSignalSpy closed(&view, &EmbeddedSettingsView::closeRequested);
    javascript(view.page(), QStringLiteral("document.getElementById('close-settings').click();"));
    QTRY_COMPARE(closed.count(), 1);

    // A foreign document can never inherit the privileged settings channel.
    QSignalSpy navigation(&view, &QWebEngineView::loadFinished);
    view.load(QUrl(QStringLiteral("https://example.invalid/whatly-settings-test")));
    if (navigation.isEmpty())
      QVERIFY(navigation.wait(10000));
    QVERIFY(!navigation.last().first().toBool());
    QCOMPARE(view.url(), QUrl(QStringLiteral("qrc:/whatly/settings/settings.html")));
  }

  void mainWindowPanelSurvivesPointerFocusOcclusionAndResize() {
    SetupWizard::markCompleted();
    UpdateChecker::setEnabled(false);
    QSettings &saved = SettingsManager::instance().settings();
    saved.setValue(QStringLiteral("hideTrayIcon"), true);
    saved.setValue(QStringLiteral("normalGeometry"), QRect(0, 0, 1200, 850));
    saved.setValue(QStringLiteral("appAutoLocking"), false);

    // MainWindow creates the real account, page scripts and WebChannel. Block
    // its initial WhatsApp navigation before constructing it, then supply only
    // synthetic markup under the URL expected by its production origin guard.
    BlockRemoteRequests blocker;
    QWebEngineProfile *profile = WebEngineProfileManager::instance().profile();
    profile->setUrlRequestInterceptor(&blocker);
    const auto resetInterceptor = qScopeGuard([profile] {
      profile->setUrlRequestInterceptor(nullptr);
    });
    MainWindow window;
    window.resize(1200, 850);
    window.show();
    auto *account = window.findChild<WebView *>();
    auto *settings = window.findChild<SettingsWidget *>();
    QVERIFY(account && settings);
    account->stop();
    QTest::qWait(100); // let the account's queued native reparent/show settle
    const QString html = QStringLiteral(R"HTML(
      <!doctype html><html><head><meta charset="utf-8"><style>
      html,body{margin:0;height:100%;font:15px Arial;background:#fff;color:#111b21}
      #app{display:flex;width:100vw;height:100vh}
      #rail{flex:0 0 64px;background:#f0f2f5;display:flex;flex-direction:column;justify-content:flex-end}
      #rail button{width:48px;height:48px;margin:4px 8px;border:0}
      #settings-column{flex:0 0 320px;overflow:auto;background:#fff}
      h1{padding:20px 24px;margin:0;font-size:24px}
      #settings-list{display:flex;flex-direction:column}
      .native-row{display:flex;align-items:center;width:100%;min-height:60px;padding:14px 24px;gap:24px;border:0;background:transparent;color:inherit;font:inherit}
      .native-row [data-icon]{display:inline-block;width:24px;height:24px}
      #main{flex:1;min-width:0;background:#f7f9fa}
      </style></head><body><div id="app">
      <nav id="rail"><button id="settings-nav" aria-label="Settings" aria-pressed="true"><span data-icon="settings">S</span></button><button id="profile-nav" aria-label="Profile">P</button></nav>
      <section id="settings-column"><h1>Settings</h1><div id="settings-list" data-testid="settings-list">
      <button class="native-row" id="account-row"><span data-icon="account">A</span><span>Account</span></button>
      <button class="native-row" id="privacy-row"><span data-icon="privacy">P</span><span>Privacy</span></button>
      <button class="native-row" id="logout-row"><span data-icon="exit">X</span><span>Log out</span></button>
      </div></section><main id="main">Synthetic conversation area</main></div></body></html>
    )HTML");
    QSignalSpy loaded(account, &QWebEngineView::loadFinished);
    account->setHtml(html, QUrl(QStringLiteral("https://web.whatsapp.com/")));
    QVERIFY(loaded.wait(15000));
    QVERIFY(loaded.last().first().toBool());
    QCOMPARE(account->url().host(), QStringLiteral("web.whatsapp.com"));
    QTRY_VERIFY_WITH_TIMEOUT(javascript(account->page(), QStringLiteral(
        "!!document.getElementById('whatly-settings-row') && !!window.__whatlySettingsHost")).toBool(), 10000);

    const auto center = [](QWebEngineView *view, const QString &selector) {
      const QVariantList point = javascript(view->page(), QStringLiteral(
          "(() => { const n = document.querySelector('%1'); if (!n) return [-1,-1];"
          "const r = n.getBoundingClientRect(); return [r.x+r.width/2,r.y+r.height/2]; })()")
          .arg(selector)).toList();
      return point.size() == 2 ? QPoint(qRound(point[0].toDouble() * view->zoomFactor()),
                                        qRound(point[1].toDouble() * view->zoomFactor())) : QPoint(-1, -1);
    };
    const auto click = [&center](QWebEngineView *view, const QString &selector) {
      QWidget *target = view->focusProxy() ? view->focusProxy() : view;
      const QPoint point = center(view, selector);
      QTest::mouseMove(target, point);
      QTest::mouseClick(target, Qt::LeftButton, Qt::NoModifier, point);
    };
    window.activateWindow();
    account->setFocus();
    click(account, QStringLiteral("#whatly-settings-row"));
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<EmbeddedSettingsView *>() &&
        window.findChild<EmbeddedSettingsView *>()->isVisible(), 10000);
    auto *panel = window.findChild<EmbeddedSettingsView *>();
    QVERIFY(panel);
    QTRY_VERIFY_WITH_TIMEOUT(javascript(panel->page(), QStringLiteral(
        "!!document.querySelector('[data-control-id=\"smoothScrollingCheckBox\"] input')")).toBool(), 10000);

    // React can replace a native row just from hover. Drive the real browser
    // pointer into that row and confirm the host never closes the child form.
    javascript(account->page(), QStringLiteral(
        "window.pointerReplacements=0;"
        "document.getElementById('privacy-row').addEventListener('pointerenter',()=>{"
        "const logout=document.getElementById('logout-row');"
        "logout.replaceWith(logout.cloneNode(true)); window.pointerReplacements++; },{once:true});"));
    QTest::mouseMove(account->focusProxy(), center(account, QStringLiteral("#privacy-row")));
    QTRY_COMPARE(javascript(account->page(), QStringLiteral("window.pointerReplacements")).toInt(), 1);
    QTest::qWait(300);
    QVERIFY2(panel->isVisible(), "Hover replacement of the logout row must not close settings");

    // Use native input events across both WebEngine surfaces, not just DOM
    // clicks: moving into the separate child renderer must not dismiss it.
    QTest::mouseMove(account->focusProxy(), QPoint(25, 50));
    QTest::mouseMove(panel->focusProxy(), QPoint(90, 50));
    click(panel, QStringLiteral("#settings-search"));
    QTest::keyClicks(panel->focusProxy(), QStringLiteral("smooth scrolling"));
    QTRY_VERIFY(javascript(panel->page(), QStringLiteral(
        "document.querySelector('[data-control-id=\"smoothScrollingCheckBox\"] input').getBoundingClientRect().height > 0")).toBool());
    auto *checkbox = settings->findChild<QCheckBox *>(QStringLiteral("smoothScrollingCheckBox"));
    QVERIFY(checkbox);
    const bool next = !checkbox->isChecked();
    click(panel, QStringLiteral("[data-control-id=\"smoothScrollingCheckBox\"] input"));
    QTRY_COMPARE(checkbox->isChecked(), next);
    QVERIFY(panel->isVisible());
    QVERIFY(!settings->isVisible());

    // Offscreen Qt does not expose compositor occlusion. Reproduce its document
    // visibility notification while retaining the real source/panel hierarchy.
    javascript(account->page(), QStringLiteral(
        "Object.defineProperty(document,'hidden',{configurable:true,get:()=>true});"
        "document.dispatchEvent(new Event('visibilitychange'));"));
    QTest::qWait(300);
    QVERIFY2(panel->isVisible(), "Native child occlusion must not close settings");
    javascript(account->page(), QStringLiteral("delete document.hidden;"));

    // React may replace or reorder the settings list while the pointer is over
    // it. A new native list must preserve the already open child settings view.
    javascript(account->page(), QStringLiteral(
        "(() => { const old = document.getElementById('settings-list');"
        "const fresh = old.cloneNode(true); fresh.querySelector('#whatly-settings-row')?.remove();"
        "old.replaceWith(fresh); })();"));
    QTest::qWait(400);
    QVERIFY2(panel->isVisible(), "Replacing the native list must not close settings");
    QTRY_COMPARE(javascript(account->page(), QStringLiteral(
        "document.querySelectorAll('#whatly-settings-row').length")).toInt(), 1);

    const int previousWidth = panel->width();
    window.resize(1080, 760);
    QTRY_VERIFY(panel->width() < previousWidth);
    QVERIFY(panel->isVisible());
    QTest::mouseMove(account->focusProxy(), QPoint(25, 50));
    QTest::mouseMove(panel->focusProxy(), QPoint(90, 50));
    QTest::qWait(200);
    QVERIFY(panel->isVisible());

    click(panel, QStringLiteral("#close-settings"));
    QTRY_VERIFY(!panel->isVisible());
    account->setFocus();
    click(account, QStringLiteral("#whatly-settings-row"));
    QTRY_VERIFY(panel->isVisible());
    click(account, QStringLiteral("#profile-nav"));
    QTRY_VERIFY(!panel->isVisible());
  }

  void browserFixtures_data() {
    QTest::addColumn<QString>("fixture");
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("expectedAssertions");
    QTest::newRow("navigation") << QStringLiteral("settings-navigation.html") << 1280 << 36;
    QTest::newRow("panel-1000px") << QStringLiteral("settings-panel.html") << 1000 << 35;
    QTest::newRow("panel-520px") << QStringLiteral("settings-panel.html") << 520 << 35;
    QTest::newRow("panel-320px") << QStringLiteral("settings-panel.html") << 320 << 35;
  }

  void browserFixtures() {
    QFETCH(QString, fixture);
    QFETCH(int, width);
    QFETCH(int, expectedAssertions);
    const QString path = QStringLiteral(WHATLY_SOURCE_DIR "/tests/fixtures/") + fixture;
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));

    // Each fixture gets its own memory-only browser profile. Local fixture
    // scripts are allowed; external resources and personal browser data are not.
    QWebEngineProfile profile;
    QWebEngineView view;
    QWebEnginePage page(&profile);
    page.settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    page.settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    view.setPage(&page);
    view.resize(width, 850);
    view.show();
    QSignalSpy loaded(&view, &QWebEngineView::loadFinished);
    view.load(QUrl::fromLocalFile(path));
    QVERIFY(loaded.wait(10000));
    QVERIFY(loaded.last().first().toBool());
    QTRY_VERIFY_WITH_TIMEOUT(javascript(&page, QStringLiteral("window.testDone === true")).toBool(), 30000);
    const QVariantMap report = javascript(&page, QStringLiteral(
        "({results: window.testResults, error: window.testError || ''})")).toMap();
    const QString error = report.value(QStringLiteral("error")).toString();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QVariantList results = report.value(QStringLiteral("results")).toList();
    QCOMPARE(results.size(), expectedAssertions);
    for (const QVariant &entry : results) {
      const QVariantMap assertion = entry.toMap();
      const QString name = assertion.value(QStringLiteral("name")).toString();
      QVERIFY2(assertion.value(QStringLiteral("passed")).toBool(), qPrintable(name));
    }
  }
};

QTEST_MAIN(TstEmbeddedSettingsBridge)
#include "tst_embeddedsettingsbridge.moc"
