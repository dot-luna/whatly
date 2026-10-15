#include "embeddedsettingsbridge.h"

#include "dictionaryrows.h"
#include "settingssearch.h"
#include "settingswidget.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSpinBox>
#include <QTextDocumentFragment>
#include <QTimeEdit>
#include <QTimer>
#include <QToolButton>
#include <cmath>
#include <limits>

namespace {

QString plainText(const QString &text) {
  const QString plain = text.contains(QLatin1Char('<'))
                            ? QTextDocumentFragment::fromHtml(text).toPlainText()
                            : text;
  // Qt accelerators use a single ampersand; doubled ampersands mean a literal.
  QString out;
  for (int i = 0; i < plain.size(); ++i) {
    if (plain.at(i) == QLatin1Char('&')) {
      if (i + 1 < plain.size() && plain.at(i + 1) == QLatin1Char('&'))
        ++i;
      else
        continue;
    }
    out += plain.at(i);
  }
  return out.trimmed();
}

QVariantMap result(bool ok, const QString &error = {}) {
  return {{QStringLiteral("ok"), ok}, {QStringLiteral("error"), error}};
}

bool integerValue(const QVariant &value, int *number) {
  if (value.metaType().id() != QMetaType::Int &&
      value.metaType().id() != QMetaType::LongLong &&
      value.metaType().id() != QMetaType::Double &&
      value.metaType().id() != QMetaType::UInt)
    return false;
  bool valid = false;
  const double candidate = value.toDouble(&valid);
  if (!valid || !std::isfinite(candidate) || std::floor(candidate) != candidate ||
      candidate < std::numeric_limits<int>::min() ||
      candidate > std::numeric_limits<int>::max())
    return false;
  *number = static_cast<int>(candidate);
  return true;
}

bool sensitiveField(const QLineEdit *edit) {
  // Keep secrets redacted even while the native password-reveal timer changes
  // its echo mode. The reveal button itself is not exported.
  static const QSet<QString> secrets = {
      QStringLiteral("current_password"), QStringLiteral("translateApiKeyLineEdit"),
      QStringLiteral("aiApiKeyLineEdit"), QStringLiteral("proxyPasswordLineEdit"),
      QStringLiteral("cloudTokenEdit"), QStringLiteral("localApiTokenEdit"),
      QStringLiteral("webhookVerifyTokenEdit"), QStringLiteral("webhookAppSecretEdit")};
  return secrets.contains(edit->objectName()) ||
         edit->echoMode() != QLineEdit::Normal;
}

bool allowedControl(QWidget *widget) {
  // Deliberately enumerated: a future native control does not silently become a
  // callable WebChannel operation, and internal combo/spin-box editors stay out.
  static const QSet<QString> names = {
      QStringLiteral("styleComboBox"),
      QStringLiteral("automaticThemeCheckBox"),
      QStringLiteral("themeComboBox"),
      QStringLiteral("followSystemThemeCheckBox"),
      QStringLiteral("notificationCombo"),
      QStringLiteral("tryNotification"),
      QStringLiteral("notificationCheckBox"),
      QStringLiteral("notificationTimeOutspinBox"),
      QStringLiteral("notificationBackendComboBox"),
      QStringLiteral("notificationSoundCheckBox"),
      QStringLiteral("dndCheckBox"),
      QStringLiteral("dndStartTimeEdit"),
      QStringLiteral("dndEndTimeEdit"),
      QStringLiteral("keywordsLineEdit"),
      QStringLiteral("vipContactsLineEdit"),
      QStringLiteral("mutedContactsLineEdit"),
      QStringLiteral("inlineReplyCheckBox"),
      QStringLiteral("useNativeFileDialog"),
      QStringLiteral("muteAudioCheckBox"),
      QStringLiteral("autoPlayMediaCheckBox"),
      QStringLiteral("startMinimized"),
      QStringLiteral("rememberWindowLayoutCheckBox"),
      QStringLiteral("minimizeOnlyFocusedWindowCheckBox"),
      QStringLiteral("minimizeOnTrayIconClick"),
      QStringLiteral("unreadCountMutedCheckBox"),
      QStringLiteral("unreadCountArchivedCheckBox"),
      QStringLiteral("unreadCountMessagesCheckBox"),
      QStringLiteral("dismissEmojiPanelCheckBox"),
      QStringLiteral("languageComboBox"),
      QStringLiteral("themeToggleButtonCheckBox"),
      QStringLiteral("privacyBlurButtonCheckBox"),
      QStringLiteral("zoomButtonsCheckBox"),
      QStringLiteral("chatListStripButtonCheckBox"),
      QStringLiteral("chatListPreviewSizeComboBox"),
      QStringLiteral("monochromeTrayIconCheckBox"),
      QStringLiteral("monochromeBadgeRedCheckBox"),
      QStringLiteral("smoothScrollingCheckBox"),
      QStringLiteral("chooseCustomCssButton"),
      QStringLiteral("clearCustomCssButton"),
      QStringLiteral("hideTrayIconCheckBox"),
      QStringLiteral("fontFamilyComboBox"),
      QStringLiteral("hideMutedStatusCheckBox"),
      QStringLiteral("spellCheckCheckBox"),
      QStringLiteral("spellCheckLanguageComboBox"),
      QStringLiteral("interfaceFontSizeSpinBox"),
      QStringLiteral("autoRestartCheckBox"),
      QStringLiteral("privacyBlurComboBox"),
      QStringLiteral("chatThemeComboBox"),
      QStringLiteral("chooseChatWallpaperButton"),
      QStringLiteral("clearChatWallpaperButton"),
      QStringLiteral("identifyInLinkedDevicesCheckBox"),
      QStringLiteral("linkedDeviceBrowserLineEdit"),
      QStringLiteral("userAgentLineEdit"),
      QStringLiteral("setUserAgent"),
      QStringLiteral("defaultUserAgentButton"),
      QStringLiteral("zoomMinus"),
      QStringLiteral("zoomFactorSpinBox"),
      QStringLiteral("zoomPlus"),
      QStringLiteral("zoomReset"),
      QStringLiteral("zoomMinusMaximized"),
      QStringLiteral("zoomFactorSpinBoxMaximized"),
      QStringLiteral("zoomPlusMaximized"),
      QStringLiteral("zoomResetMaximized"),
      QStringLiteral("closeButtonActionComboBox"),
      QStringLiteral("showShortcutsButton"),
      QStringLiteral("showPermissionsButton"),
      QStringLiteral("applock_checkbox"),
      QStringLiteral("lockOnMinimizeCheckBox"),
      QStringLiteral("lockOnScreenLockCheckBox"),
      QStringLiteral("chnageCurrentPasswordPushButton"),
      QStringLiteral("appAutoLockcheckBox"),
      QStringLiteral("autoLockDurationSpinbox"),
      QStringLiteral("resetAppAutoLockPushButton"),
      QStringLiteral("current_password"),
      QStringLiteral("defaultDownloadLocation"),
      QStringLiteral("changeDefaultDownloadLocationPb"),
      QStringLiteral("deletePersistentData"),
      QStringLiteral("clearCacheButton"),
      QStringLiteral("exportProfileButton"),
      QStringLiteral("importProfileButton"),
      QStringLiteral("disableGpuCheckBox"),
      QStringLiteral("disableGpuCompositingCheckBox"),
      QStringLiteral("disableGpuVsyncCheckBox"),
      QStringLiteral("inProcessGpuCheckBox"),
      QStringLiteral("ignoreGpuBlocklistCheckBox"),
      QStringLiteral("singleProcessCheckBox"),
      QStringLiteral("processPerSiteCheckBox"),
      QStringLiteral("optimizeForSizeCheckBox"),
      QStringLiteral("focusModeCheckBox"),
      QStringLiteral("hdMediaCheckBox"),
      QStringLiteral("undoSendCheckBox"),
      QStringLiteral("undoSendSecondsSpinBox"),
      QStringLiteral("translateEnabledCheckBox"),
      QStringLiteral("translateEndpointLineEdit"),
      QStringLiteral("translateApiKeyLineEdit"),
      QStringLiteral("translateTargetLineEdit"),
      QStringLiteral("aiEnabledCheckBox"),
      QStringLiteral("aiEndpointLineEdit"),
      QStringLiteral("aiModelLineEdit"),
      QStringLiteral("aiApiKeyLineEdit"),
      QStringLiteral("aiDetectButton"),
      QStringLiteral("aiInstalledModelsCombo"),
      QStringLiteral("aiRecommendedCombo"),
      QStringLiteral("aiDownloadButton"),
      QStringLiteral("aiPullProgressBar"),
      QStringLiteral("webrtcShieldCheckBox"),
      QStringLiteral("jsMemoryLimitSpinBox"),
      QStringLiteral("cacheTypeComboBox"),
      QStringLiteral("cacheMaxSpinBox"),
      QStringLiteral("fontHintingComboBox"),
      QStringLiteral("suspendInactiveAccountsCheckBox"),
      QStringLiteral("suspendAfterSpinBox"),
      QStringLiteral("unloadOffscreenWindowsCheckBox"),
      QStringLiteral("autostartCheckBox"),
      QStringLiteral("customWindowFrameCheckBox"),
      QStringLiteral("restartNowButton"),
      QStringLiteral("alwaysShowAccountTabsCheckBox"),
      QStringLiteral("tabsInTitleBarCheckBox"),
      QStringLiteral("checkUpdatesCheckBox"),
      QStringLiteral("interfaceScaleSpinBox"),
      QStringLiteral("proxyModeComboBox"),
      QStringLiteral("proxyHostLineEdit"),
      QStringLiteral("proxyPortSpinBox"),
      QStringLiteral("proxyUserLineEdit"),
      QStringLiteral("proxyPasswordLineEdit"),
      QStringLiteral("jsAddonsList"),
      QStringLiteral("addJsAddonButton"),
      QStringLiteral("removeJsAddonButton"),
      QStringLiteral("cannedList"),
      QStringLiteral("addCannedButton"),
      QStringLiteral("removeCannedButton"),
      QStringLiteral("cloudPhoneIdEdit"),
      QStringLiteral("cloudTokenEdit"),
      QStringLiteral("cloudApiVersionEdit"),
      QStringLiteral("localApiEnabledCheckBox"),
      QStringLiteral("localApiPortSpinBox"),
      QStringLiteral("localApiTokenEdit"),
      QStringLiteral("webhookEnabledCheckBox"),
      QStringLiteral("webhookVerifyTokenEdit"),
      QStringLiteral("webhookAppSecretEdit"),
      QStringLiteral("collectDiagnosticsCheckBox"),
      QStringLiteral("copyDiagnosticsButton"),
      QStringLiteral("cookieSize"),
      QStringLiteral("cacheSize"),
      QStringLiteral("aiOllamaStatusLabel"),
      QStringLiteral("aiPullStatusLabel"),
      QStringLiteral("performanceHintLabel"),
      QStringLiteral("jsAddonsHintLabel"),
      QStringLiteral("cannedHintLabel"),
      QStringLiteral("shortcutsHintLabel"),
      QStringLiteral("cloudApiHintLabel"),
      QStringLiteral("localApiHintLabel"),
  };
  if (names.contains(widget->objectName()))
    return true;
  if (qobject_cast<QKeySequenceEdit *>(widget))
    return widget->objectName().startsWith(QStringLiteral("shortcut/"));
  if (qobject_cast<QPushButton *>(widget)) {
    static const QRegularExpression restartName(QStringLiteral("^embeddedRestart[0-9]+$"));
    return restartName.match(widget->objectName()).hasMatch();
  }
  return false;
}

QString fallbackLabel(QWidget *widget) {
  static const QHash<QString, QString> labels = {
      {QStringLiteral("defaultUserAgentButton"), QObject::tr("Restore default user agent")},
      {QStringLiteral("zoomMinus"), QObject::tr("Decrease normal window zoom")},
      {QStringLiteral("zoomPlus"), QObject::tr("Increase normal window zoom")},
      {QStringLiteral("zoomReset"), QObject::tr("Reset normal window zoom")},
      {QStringLiteral("zoomMinusMaximized"), QObject::tr("Decrease maximized window zoom")},
      {QStringLiteral("zoomPlusMaximized"), QObject::tr("Increase maximized window zoom")},
      {QStringLiteral("zoomResetMaximized"), QObject::tr("Reset maximized window zoom")},
      {QStringLiteral("spellCheckLanguageComboBox"), QObject::tr("Spelling languages and dictionaries")},
      {QStringLiteral("jsAddonsList"), QObject::tr("Installed JavaScript addons")},
      {QStringLiteral("cannedList"), QObject::tr("Saved replies")}};
  if (labels.contains(widget->objectName()))
    return labels.value(widget->objectName());
  QString label = widget->objectName();
  label.replace(QRegularExpression(QStringLiteral("([a-z])([A-Z])")), QStringLiteral("\\1 \\2"));
  label.replace(QRegularExpression(QStringLiteral(
      "(Line Edit|Combo Box|Spin Box|Push Button|Check Box|Edit|Label)$")), QString());
  if (!label.isEmpty())
    label[0] = label.at(0).toUpper();
  return label.trimmed();
}

} // namespace

EmbeddedSettingsBridge::EmbeddedSettingsBridge(SettingsWidget *settings,
                                               QObject *parent)
    : QObject(parent), m_settings(settings) {
  buildControls();
}

void EmbeddedSettingsBridge::buildControls() {
  if (!m_settings)
    return;
  QWidget *content = m_settings->findChild<QWidget *>(QStringLiteral("scrollAreaWidgetContents"));
  if (!content || !content->layout())
    return;

  // Read the accordion in layout order. Its bodies may be collapsed and the
  // SettingsWidget remains hidden; neither condition disables their settings.
  for (int i = 0; i < content->layout()->count(); ++i) {
    QWidget *wrapper = content->layout()->itemAt(i)->widget();
    if (!wrapper)
      continue;
    QGroupBox *body = wrapper->findChild<QGroupBox *>(QString(), Qt::FindDirectChildrenOnly);
    QToolButton *header = wrapper->findChild<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
    if (!body || !header)
      continue;
    Section section{body->objectName(), plainText(header->text()), {}};
    for (const SettingsSearch::Row &row : SettingsSearch::rowsOf(body)) {
      QString rowLabel;
      for (QWidget *widget : row.widgets) {
        if (auto *label = qobject_cast<QLabel *>(widget)) {
          if (!allowedControl(label) && !plainText(label->text()).isEmpty()) {
            rowLabel = plainText(label->text());
            break;
          }
        }
      }
      for (QWidget *widget : row.widgets) {
        if (!allowedControl(widget) || m_controls.contains(widget->objectName()))
          continue;
        QString label = rowLabel;
        if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
          const QString text = plainText(button->text());
          if (!text.isEmpty())
            label = rowLabel.isEmpty() ? text : rowLabel + QStringLiteral(": ") + text;
        }
        if (label.isEmpty())
          label = fallbackLabel(widget);
        QString group;
        if (auto *nested = qobject_cast<QGroupBox *>(row.container))
          group = plainText(nested->title());
        const QString id = widget->objectName();
        m_controls.insert(id, {widget, label, group, rowLabel});
        section.controls << id;
        observe(widget);
      }
    }
    if (!section.controls.isEmpty())
      m_sections << section;
  }
}

void EmbeddedSettingsBridge::observe(QWidget *widget) {
  widget->installEventFilter(this);
  if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
    connect(button, &QAbstractButton::toggled, this, &EmbeddedSettingsBridge::queueChanged);
    connect(button, &QAbstractButton::clicked, this, &EmbeddedSettingsBridge::queueChanged);
  } else if (auto *combo = qobject_cast<QComboBox *>(widget)) {
    connect(combo, &QComboBox::currentIndexChanged, this, &EmbeddedSettingsBridge::queueChanged);
    connect(combo->model(), &QAbstractItemModel::dataChanged, this, &EmbeddedSettingsBridge::queueChanged);
    connect(combo->model(), &QAbstractItemModel::rowsInserted, this, &EmbeddedSettingsBridge::queueChanged);
    connect(combo->model(), &QAbstractItemModel::rowsRemoved, this, &EmbeddedSettingsBridge::queueChanged);
    connect(combo->model(), &QAbstractItemModel::modelReset, this, &EmbeddedSettingsBridge::queueChanged);
  } else if (auto *spin = qobject_cast<QSpinBox *>(widget)) {
    connect(spin, &QSpinBox::valueChanged, this, &EmbeddedSettingsBridge::queueChanged);
  } else if (auto *spin = qobject_cast<QDoubleSpinBox *>(widget)) {
    connect(spin, &QDoubleSpinBox::valueChanged, this, &EmbeddedSettingsBridge::queueChanged);
  } else if (auto *edit = qobject_cast<QLineEdit *>(widget)) {
    connect(edit, &QLineEdit::textChanged, this, &EmbeddedSettingsBridge::queueChanged);
  } else if (auto *edit = qobject_cast<QTimeEdit *>(widget)) {
    connect(edit, &QTimeEdit::timeChanged, this, &EmbeddedSettingsBridge::queueChanged);
  } else if (auto *edit = qobject_cast<QKeySequenceEdit *>(widget)) {
    connect(edit, &QKeySequenceEdit::keySequenceChanged, this, &EmbeddedSettingsBridge::queueChanged);
  } else if (auto *list = qobject_cast<QListWidget *>(widget)) {
    connect(list, &QListWidget::currentRowChanged, this, &EmbeddedSettingsBridge::queueChanged);
    connect(list->model(), &QAbstractItemModel::dataChanged, this, &EmbeddedSettingsBridge::queueChanged);
    connect(list->model(), &QAbstractItemModel::rowsInserted, this, &EmbeddedSettingsBridge::queueChanged);
    connect(list->model(), &QAbstractItemModel::rowsRemoved, this, &EmbeddedSettingsBridge::queueChanged);
    connect(list->model(), &QAbstractItemModel::modelReset, this, &EmbeddedSettingsBridge::queueChanged);
  } else if (auto *progress = qobject_cast<QProgressBar *>(widget)) {
    connect(progress, &QProgressBar::valueChanged, this, &EmbeddedSettingsBridge::queueChanged);
  }
}

bool EmbeddedSettingsBridge::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::EnabledChange || event->type() == QEvent::ToolTipChange)
    queueChanged();
  return QObject::eventFilter(watched, event);
}

void EmbeddedSettingsBridge::queueChanged() {
  if (m_updateQueued)
    return;
  m_updateQueued = true;
  QTimer::singleShot(0, this, [this] {
    m_updateQueued = false;
    emit changed(snapshot());
  });
}

QVariantMap EmbeddedSettingsBridge::describe(const QString &id, const Control &control) const {
  QWidget *widget = control.widget;
  if (!widget)
    return {};
  QVariantMap out{{QStringLiteral("id"), id}, {QStringLiteral("label"), control.label},
                  {QStringLiteral("group"), control.group},
                  {QStringLiteral("enabled"), widget->isEnabled()},
                  {QStringLiteral("readOnly"), false},
                  {QStringLiteral("description"), plainText(widget->toolTip())}};
  if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
    const QString text = plainText(button->text());
    if (!text.isEmpty())
      out.insert(QStringLiteral("label"), control.rowLabel.isEmpty() ? text : control.rowLabel + QStringLiteral(": ") + text);
    out.insert(QStringLiteral("type"), button->isCheckable() ? QStringLiteral("toggle") : QStringLiteral("button"));
    out.insert(QStringLiteral("value"), button->isChecked());
  } else if (auto *combo = qobject_cast<QComboBox *>(widget)) {
    const bool dictionaries = id == QLatin1String("spellCheckLanguageComboBox");
    out.insert(QStringLiteral("type"), dictionaries ? QStringLiteral("multiselect") : QStringLiteral("select"));
    out.insert(QStringLiteral("value"), combo->currentIndex());
    QVariantList options;
    for (int i = 0; i < combo->count(); ++i) {
      const QModelIndex index = combo->model()->index(i, combo->modelColumn(), combo->rootModelIndex());
      const Qt::ItemFlags flags = combo->model()->flags(index);
      QVariantMap option{{QStringLiteral("label"), plainText(combo->itemText(i))},
                         {QStringLiteral("value"), i},
                         {QStringLiteral("enabled"), flags.testFlag(Qt::ItemIsEnabled)}};
      if (dictionaries) {
        option.insert(QStringLiteral("checkable"), flags.testFlag(Qt::ItemIsUserCheckable));
        option.insert(QStringLiteral("checked"), index.data(Qt::CheckStateRole).toInt() == Qt::Checked);
        option.insert(QStringLiteral("description"), plainText(index.data(Qt::ToolTipRole).toString()));
        const int progress = index.data(DictionaryRows::ProgressRole).toInt();
        const auto action = static_cast<DictionaryRows::Action>(index.data(DictionaryRows::ActionRole).toInt());
        option.insert(QStringLiteral("progress"), progress);
        if (progress < 0 && (action != DictionaryRows::Action::None ||
                            index.data(DictionaryRows::CodeRole).toString().isEmpty())) {
          option.insert(QStringLiteral("actionId"), QStringLiteral("dictionary/") + QString::number(i));
          option.insert(QStringLiteral("actionLabel"),
                        action == DictionaryRows::Action::Download ? tr("Download") :
                        action == DictionaryRows::Action::Delete ? tr("Delete") : tr("Retry"));
        }
      }
      options << option;
    }
    out.insert(QStringLiteral("options"), options);
  } else if (auto *spin = qobject_cast<QSpinBox *>(widget)) {
    out.insert(QStringLiteral("type"), QStringLiteral("number"));
    out.insert(QStringLiteral("value"), spin->value());
    out.insert(QStringLiteral("minimum"), spin->minimum());
    out.insert(QStringLiteral("maximum"), spin->maximum());
    out.insert(QStringLiteral("step"), spin->singleStep());
    out.insert(QStringLiteral("suffix"), spin->suffix());
    out.insert(QStringLiteral("prefix"), spin->prefix());
    out.insert(QStringLiteral("readOnly"), spin->isReadOnly());
  } else if (auto *spin = qobject_cast<QDoubleSpinBox *>(widget)) {
    out.insert(QStringLiteral("type"), QStringLiteral("number"));
    out.insert(QStringLiteral("value"), spin->value());
    out.insert(QStringLiteral("minimum"), spin->minimum());
    out.insert(QStringLiteral("maximum"), spin->maximum());
    out.insert(QStringLiteral("step"), spin->singleStep());
    out.insert(QStringLiteral("suffix"), spin->suffix());
    out.insert(QStringLiteral("prefix"), spin->prefix());
    out.insert(QStringLiteral("readOnly"), spin->isReadOnly());
  } else if (auto *edit = qobject_cast<QLineEdit *>(widget)) {
    const bool secret = sensitiveField(edit);
    out.insert(QStringLiteral("type"), secret ? QStringLiteral("password") : QStringLiteral("text"));
    out.insert(QStringLiteral("value"), secret ? QString() : edit->text());
    out.insert(QStringLiteral("configured"), secret && !edit->text().isEmpty());
    out.insert(QStringLiteral("readOnly"), edit->isReadOnly());
    out.insert(QStringLiteral("placeholder"), secret ? QString() : edit->placeholderText());
    out.insert(QStringLiteral("maximumLength"), edit->maxLength());
  } else if (auto *edit = qobject_cast<QTimeEdit *>(widget)) {
    out.insert(QStringLiteral("type"), QStringLiteral("time"));
    out.insert(QStringLiteral("value"), edit->time().toString(QStringLiteral("HH:mm")));
    out.insert(QStringLiteral("readOnly"), edit->isReadOnly());
  } else if (auto *edit = qobject_cast<QKeySequenceEdit *>(widget)) {
    out.insert(QStringLiteral("type"), QStringLiteral("shortcut"));
    out.insert(QStringLiteral("value"), edit->keySequence().toString(QKeySequence::PortableText));
  } else if (auto *list = qobject_cast<QListWidget *>(widget)) {
    out.insert(QStringLiteral("type"), QStringLiteral("list"));
    out.insert(QStringLiteral("value"), list->currentRow());
    QVariantList options;
    for (int i = 0; i < list->count(); ++i) {
      QListWidgetItem *item = list->item(i);
      options << QVariantMap{{QStringLiteral("label"), plainText(item->text())},
                             {QStringLiteral("value"), i},
                             {QStringLiteral("enabled"), item->flags().testFlag(Qt::ItemIsEnabled)},
                             {QStringLiteral("checkable"),
                              item->flags().testFlag(Qt::ItemIsUserCheckable) &&
                              item->data(Qt::CheckStateRole).isValid()},
                             {QStringLiteral("checked"), item->checkState() == Qt::Checked}};
    }
    out.insert(QStringLiteral("options"), options);
  } else if (auto *label = qobject_cast<QLabel *>(widget)) {
    out.insert(QStringLiteral("type"), QStringLiteral("info"));
    out.insert(QStringLiteral("readOnly"), true);
    out.insert(QStringLiteral("value"), plainText(label->text()));
  } else if (auto *progress = qobject_cast<QProgressBar *>(widget)) {
    out.insert(QStringLiteral("type"), QStringLiteral("info"));
    out.insert(QStringLiteral("readOnly"), true);
    out.insert(QStringLiteral("value"), progress->text());
  }
  return out;
}

QVariantMap EmbeddedSettingsBridge::snapshot() const {
  QVariantList sections;
  for (const Section &section : m_sections) {
    QVariantList controls;
    for (const QString &id : section.controls) {
      const QVariantMap control = describe(id, m_controls.value(id));
      if (!control.isEmpty())
        controls << control;
    }
    sections << QVariantMap{{QStringLiteral("id"), section.id},
                            {QStringLiteral("title"), section.title},
                            {QStringLiteral("controls"), controls}};
  }
  return {{QStringLiteral("title"), tr("Whatly Settings")},
          {QStringLiteral("sections"), sections}};
}

QVariantMap EmbeddedSettingsBridge::setValue(const QString &id, const QVariant &value) {
  QWidget *widget = m_controls.value(id).widget;
  if (!widget || !m_settings)
    return result(false, tr("Unknown setting."));
  if (!widget->isEnabled() || describe(id, m_controls.value(id)).value(QStringLiteral("readOnly")).toBool())
    return result(false, tr("This setting is not editable right now."));
  if (auto *check = qobject_cast<QCheckBox *>(widget)) {
    if (value.metaType().id() != QMetaType::Bool)
      return result(false, tr("Choose on or off."));
    check->setChecked(value.toBool());
  } else if (auto *combo = qobject_cast<QComboBox *>(widget)) {
    int row = -1;
    const bool dictionaries = id == QLatin1String("spellCheckLanguageComboBox");
    const QVariantMap change = value.toMap();
    if (!integerValue(dictionaries ? change.value(QStringLiteral("index")) : value, &row) || row < 0 || row >= combo->count())
      return result(false, tr("Choose a valid option."));
    const QModelIndex index = combo->model()->index(row, combo->modelColumn(), combo->rootModelIndex());
    const Qt::ItemFlags flags = combo->model()->flags(index);
    if (!flags.testFlag(Qt::ItemIsEnabled))
      return result(false, tr("This option is unavailable."));
    if (dictionaries) {
      if (!flags.testFlag(Qt::ItemIsUserCheckable) || change.value(QStringLiteral("checked")).metaType().id() != QMetaType::Bool)
        return result(false, tr("Choose an installed spelling language."));
      combo->model()->setData(index,
          change.value(QStringLiteral("checked")).toBool() ? Qt::Checked : Qt::Unchecked,
          Qt::CheckStateRole);
    } else {
      combo->setCurrentIndex(row);
      // Most controls save currentIndexChanged; the installed AI-model picker
      // intentionally responds only to a user activation.
      emit combo->activated(row);
    }
  } else if (auto *spin = qobject_cast<QSpinBox *>(widget)) {
    int number = 0;
    if (!integerValue(value, &number) || number < spin->minimum() || number > spin->maximum())
      return result(false, tr("Enter a whole number within the allowed range."));
    spin->setValue(number);
  } else if (auto *spin = qobject_cast<QDoubleSpinBox *>(widget)) {
    if (value.metaType().id() != QMetaType::Int && value.metaType().id() != QMetaType::Double)
      return result(false, tr("Enter a number."));
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < spin->minimum() || number > spin->maximum())
      return result(false, tr("Enter a number within the allowed range."));
    spin->setValue(number);
  } else if (auto *edit = qobject_cast<QLineEdit *>(widget)) {
    if (value.metaType().id() != QMetaType::QString ||
        value.toString().size() > edit->maxLength() || value.toString().contains(QChar::Null))
      return result(false, tr("Enter valid text."));
    edit->setText(value.toString());
    emit edit->editingFinished();
  } else if (auto *edit = qobject_cast<QTimeEdit *>(widget)) {
    const QTime time = QTime::fromString(value.toString(), QStringLiteral("HH:mm"));
    if (value.metaType().id() != QMetaType::QString || !time.isValid() ||
        time.toString(QStringLiteral("HH:mm")) != value.toString() ||
        time < edit->minimumTime() || time > edit->maximumTime())
      return result(false, tr("Enter a valid time."));
    edit->setTime(time);
  } else if (auto *edit = qobject_cast<QKeySequenceEdit *>(widget)) {
    if (value.metaType().id() != QMetaType::QString || value.toString().size() > 256)
      return result(false, tr("Enter a valid shortcut."));
    const QKeySequence sequence = QKeySequence::fromString(value.toString(), QKeySequence::PortableText);
    if ((!value.toString().isEmpty() && sequence.isEmpty()) || sequence.count() > edit->maximumSequenceLength())
      return result(false, tr("Enter a valid shortcut."));
    for (int i = 0; i < sequence.count(); ++i)
      if (sequence[i].key() == Qt::Key_unknown)
        return result(false, tr("Enter a valid shortcut."));
    edit->setKeySequence(sequence);
    emit edit->editingFinished();
  } else if (auto *list = qobject_cast<QListWidget *>(widget)) {
    int row = -1;
    const bool toggle = value.metaType().id() == QMetaType::QVariantMap;
    const QVariantMap change = value.toMap();
    if (!integerValue(toggle ? change.value(QStringLiteral("index")) : value, &row) || row < 0 || row >= list->count())
      return result(false, tr("Choose a valid list item."));
    QListWidgetItem *item = list->item(row);
    if (!item->flags().testFlag(Qt::ItemIsEnabled))
      return result(false, tr("This item is unavailable."));
    if (toggle) {
      // QListWidgetItem includes the checkable flag by default even when its
      // row has no checkbox (saved replies). A real checkbox also has state.
      if (!item->flags().testFlag(Qt::ItemIsUserCheckable) ||
          !item->data(Qt::CheckStateRole).isValid() ||
          change.value(QStringLiteral("checked")).metaType().id() != QMetaType::Bool)
        return result(false, tr("This item cannot be checked."));
      item->setCheckState(change.value(QStringLiteral("checked")).toBool() ? Qt::Checked : Qt::Unchecked);
    }
    list->setCurrentRow(row);
  } else {
    return result(false, tr("This control does not accept a value."));
  }
  emit changed(snapshot());
  return result(true);
}

QVariantMap EmbeddedSettingsBridge::activate(const QString &id) {
  if (!m_settings)
    return result(false, tr("Settings are unavailable."));
  if (id.startsWith(QStringLiteral("dictionary/"))) {
    bool valid = false;
    const int row = id.mid(11).toInt(&valid);
    if (!valid || row < 0 || id != QStringLiteral("dictionary/") + QString::number(row) ||
        !m_settings->activateDictionaryAction(row))
      return result(false, tr("This dictionary action is unavailable."));
  } else {
    auto *button = qobject_cast<QAbstractButton *>(m_controls.value(id).widget.data());
    if (!button || button->isCheckable())
      return result(false, tr("Unknown settings action."));
    if (!button->isEnabled())
      return result(false, tr("This action is unavailable right now."));
    button->click();
  }
  emit changed(snapshot());
  return result(true);
}

void EmbeddedSettingsBridge::close() {
  emit closeRequested();
}
