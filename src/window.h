#pragma once

#include <QDialog>

#include "core.h"

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
class QMessageBox;
class QSystemTrayIcon;
class QTextEdit;
QT_END_NAMESPACE

#include "logger.h"
class Window;
class WindowLogHandler : public LoggingObserver {
protected:
  Window *window;

public:
  WindowLogHandler(Window *window);
  void HandleLogEntry(const string& entry);
};

class Window : public QDialog
{
  Q_OBJECT

public:
  Window();
  ~Window();

  void addLogEntry(const string& entry);
  WindowLogHandler& getLogHandler();

protected:
  void closeEvent(QCloseEvent *event);

private slots:
  void riseAndShine();

private:
  void createActions();
  void createTrayIcon();

  QAction *showAction;
  QAction *quitAction;

  QSystemTrayIcon *trayIcon;
  QMenu *trayIconMenu;

  QTextEdit *logText;

  WindowLogHandler logHandler;

  Core core;
};
