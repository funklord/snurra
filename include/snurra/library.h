#ifndef SNURRA_LIBRARY_H
#define SNURRA_LIBRARY_H

#include <QPoint>
#include <QStringList>
#include <QHash>
#include <QWidget>

#include <atomic>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

#include "snurra/source.h"

class QLabel;
class QLineEdit;
class QMimeData;
class QStandardItem;
class QStandardItemModel;
class QTreeView;
class library_filter;

/*
 * The music library (project.md sec 3b): what snurra opens started with
 * no file, in place of a tile with nothing to play. The audio and video
 * under a folder -- the desktop's music folder, or one chosen and then
 * remembered -- listed by name and folder and narrowed by every word
 * typed, as a DJ program's library is. A tune double-clicked (or Enter)
 * is asked to play; dragged, it carries the snurra files type, which a
 * snurra window takes as a drop and a desktop does not, so a drag let go
 * over nothing opens it as a tile of its own there.
 */
class snurra_library : public QWidget {
	Q_OBJECT

public:
	/* `folder` empty: the one chosen last, else the music folder. */
	explicit snurra_library(const QString &folder = QString(), QWidget *parent = nullptr);
	~snurra_library() override;

	QString folder() const { return m_folder; }
	/* Looked through again; `remember` keeps it for the next start. */
	void set_folder(const QString &folder, bool remember = false);
	void set_search(const QString &words);
	/* The tempo of every tune listed that has none kept, found one after
	 * another on a thread of its own and kept (beats.h): asked for, never
	 * by itself -- it holds a core a while, and a phone's battery. */
	void find_tempos();
	bool finding_tempos() const { return m_finding; }
	/* What is listed, as full paths, in the list's order; and whether
	 * the folder is still being looked through. */
	QStringList shown() const;
	bool scanning() const { return m_scanning; }

	/* A drag's data: the snurra files type, a path a line. */
	static QMimeData *mime_for(const QStringList &paths);
	/* A drag let go: over nothing, `on_dropped_nowhere`; else nothing,
	 * the drop being the window's. By default a new snurra there. */
	void dragged_out(const QStringList &paths, bool taken, const QPoint &global);
	std::function<void(const QStringList &paths, const QPoint &global)> on_dropped_nowhere;

signals:
	void play_requested(const QString &path);

protected:
	void resizeEvent(class QResizeEvent *event) override;

private:
	void fit_columns();
	void scan();
	void stop_scan();
	void found(const QStringList &paths, bool last);
	void tagged(const std::vector<std::pair<QString, snurra_tags>> &told);
	void say();

	QString m_folder;
	QLineEdit *m_search;
	QTreeView *m_view = nullptr;
	QLabel *m_status;
	QStandardItemModel *m_model;
	library_filter *m_filter;
	std::thread m_thread;
	std::thread m_tempos;
	std::atomic<bool> m_tempos_stop{ false };
	bool m_finding = false;
	class QPushButton *m_find = nullptr;
	void stop_tempos();
	std::atomic<bool> m_stop{ false };
	bool m_scanning = false;
	bool m_narrow = false;   /* Folder hidden and Artist narrowed, for the name's sake */
	QHash<QString, QStandardItem *> m_items;   /* by path: the name's item */
};

#endif
