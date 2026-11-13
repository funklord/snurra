#include "snurra/library.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTreeView>
#include <QVBoxLayout>

#include "snurra/source.h"

namespace {

constexpr int PATH_ROLE = Qt::UserRole + 1;   /* the full path, on the name */
constexpr int LISTED_MOST = 50000;            /* tunes listed */
constexpr int LOOKED_AT_MOST = 200000;        /* files looked at, a disk's worth */

/* The settings file: ~/.config/snurra/snurra.conf. Only a folder chosen
 * is kept; the music folder is followed, not written down. */
const QString FOLDER_KEY = QStringLiteral("library/folder");

QString default_folder() {
	const QString chosen = QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).value(FOLDER_KEY).toString();
	if (!chosen.isEmpty() && QFileInfo(chosen).isDir()) return chosen;
	const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
	return QFileInfo(music).isDir() ? music : QDir::homePath();
}

}   // namespace

/* Every word typed found in the tune's folder and name, case aside; in
 * path order as people count, 2 before 10. */
class library_filter : public QSortFilterProxyModel {
public:
	explicit library_filter(QObject *parent) : QSortFilterProxyModel(parent) {
		m_counting.setNumericMode(true);
		m_counting.setCaseSensitivity(Qt::CaseInsensitive);
	}
	void set_words(const QString &words) {
		m_words = words.split(QLatin1Char(' '), Qt::SkipEmptyParts);
		invalidateRowsFilter();
	}

protected:
	bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
		if (m_words.isEmpty()) return true;
		const QString text = sourceModel()->index(row, 1, parent).data().toString() + QLatin1Char('/')
		                     + sourceModel()->index(row, 0, parent).data().toString();
		for (const QString &word : m_words)
			if (!text.contains(word, Qt::CaseInsensitive)) return false;
		return true;
	}
	bool lessThan(const QModelIndex &left, const QModelIndex &right) const override {
		const QString a = sourceModel()->index(left.row(), 0).data(PATH_ROLE).toString();
		const QString b = sourceModel()->index(right.row(), 0).data(PATH_ROLE).toString();
		return m_counting.compare(a, b) < 0;
	}

private:
	QStringList m_words;
	QCollator m_counting;
};

namespace {

/* The list, whose drags carry the snurra files type and say where they
 * were let go. */
class library_view : public QTreeView {
public:
	library_view(snurra_library *library, QWidget *parent) : QTreeView(parent), m_library(library) {}

protected:
	void startDrag(Qt::DropActions) override {
		QStringList paths;
		for (const QModelIndex &index : selectionModel()->selectedRows(0)) paths << index.data(PATH_ROLE).toString();
		if (paths.isEmpty()) return;
		QDrag *drag = new QDrag(this);
		drag->setMimeData(snurra_library::mime_for(paths));
		const Qt::DropAction done = drag->exec(Qt::CopyAction);
		m_library->dragged_out(paths, done != Qt::IgnoreAction, QCursor::pos());
	}
	void keyPressEvent(QKeyEvent *event) override {
		if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && currentIndex().isValid()) {
			emit doubleClicked(currentIndex());
			return;
		}
		/* Typing goes to the search, from the list too. */
		if (!event->text().isEmpty() && event->text().at(0).isPrint() && event->key() != Qt::Key_Space) {
			if (QLineEdit *search = m_library->findChild<QLineEdit *>()) {
				search->setFocus();
				QCoreApplication::sendEvent(search, event);
				return;
			}
		}
		QTreeView::keyPressEvent(event);
	}

private:
	snurra_library *m_library;
};

}   // namespace

snurra_library::snurra_library(const QString &folder, QWidget *parent) : QWidget(parent) {
	setWindowTitle(tr("snurra -- music"));
	m_search = new QLineEdit(this);
	m_search->setPlaceholderText(tr("Search"));
	m_search->setClearButtonEnabled(true);
	QPushButton *choose = new QPushButton(tr("Folder..."), this);
	m_model = new QStandardItemModel(0, 2, this);
	m_model->setHorizontalHeaderLabels({ tr("Name"), tr("Folder") });
	m_filter = new library_filter(this);
	m_filter->setSourceModel(m_model);
	m_view = new library_view(this, this);
	m_view->setModel(m_filter);
	m_view->setRootIsDecorated(false);
	m_view->setUniformRowHeights(true);
	m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_view->setDragEnabled(true);
	m_view->setDragDropMode(QAbstractItemView::DragOnly);
	/* The name takes the room, the folder a fixed share: names were cut to
	 * a dozen letters beside an empty folder column (seen 2026-11-13). */
	m_view->header()->setStretchLastSection(false);
	m_view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_view->header()->setSectionResizeMode(1, QHeaderView::Interactive);
	m_view->header()->resizeSection(1, fontMetrics().horizontalAdvance(QLatin1Char('M')) * 14);
	m_status = new QLabel(this);
	m_status->setForegroundRole(QPalette::PlaceholderText);

	QHBoxLayout *top = new QHBoxLayout;
	top->addWidget(m_search, 1);
	top->addWidget(choose);
	QVBoxLayout *column = new QVBoxLayout(this);
	column->addLayout(top);
	column->addWidget(m_view, 1);
	column->addWidget(m_status);
	resize(640, 420);

	connect(m_search, &QLineEdit::textChanged, this, &snurra_library::set_search);
	/* Enter in the search plays what is first listed. */
	connect(m_search, &QLineEdit::returnPressed, this, [this] {
		const QModelIndex first = m_filter->index(0, 0);
		if (first.isValid()) emit play_requested(first.data(PATH_ROLE).toString());
	});
	/* A double-click, not `activated`: some styles activate on one click,
	 * and a click is for choosing what to drag. Enter is the view's own. */
	connect(m_view, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex &index) {
		emit play_requested(m_filter->index(index.row(), 0).data(PATH_ROLE).toString());
	});
	connect(choose, &QPushButton::clicked, this, [this] {
		const QString chosen = QFileDialog::getExistingDirectory(this, tr("Music folder"), m_folder);
		if (!chosen.isEmpty()) set_folder(chosen, true);
	});

	on_dropped_nowhere = [](const QStringList &paths, const QPoint &) {
		/* A new snurra opens where the pointer is (main.cpp), so there. */
		QProcess::startDetached(QCoreApplication::applicationFilePath(), paths);
	};
	m_search->setFocus();
	set_folder(folder.isEmpty() ? default_folder() : folder);
}

snurra_library::~snurra_library() {
	stop_scan();
}

void snurra_library::set_folder(const QString &folder, bool remember) {
	stop_scan();
	m_folder = QDir(folder).absolutePath();
	if (remember) QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).setValue(FOLDER_KEY, m_folder);
	m_model->removeRows(0, m_model->rowCount());
	scan();
}

void snurra_library::set_search(const QString &words) {
	if (m_search->text() != words) m_search->setText(words);   /* comes back here */
	m_filter->set_words(words);
	say();
}

QStringList snurra_library::shown() const {
	QStringList paths;
	for (int row = 0; row < m_filter->rowCount(); ++row) paths << m_filter->index(row, 0).data(PATH_ROLE).toString();
	return paths;
}

QMimeData *snurra_library::mime_for(const QStringList &paths) {
	QMimeData *mime = new QMimeData;
	mime->setData(QLatin1String(SNURRA_FILES_MIME), paths.join(QLatin1Char('\n')).toUtf8());
	return mime;
}

void snurra_library::dragged_out(const QStringList &paths, bool taken, const QPoint &global) {
	/* Let go over one of this process's windows -- the library itself --
	 * is no drop at all, not a place. */
	if (taken || QApplication::widgetAt(global) || !on_dropped_nowhere) return;
	on_dropped_nowhere(paths, global);
}

/* On a thread of its own, handed over in pieces: a large library lists as
 * it is found rather than after it all has been. */
void snurra_library::scan() {
	m_scanning = true;
	m_stop = false;
	say();
	const QString folder = m_folder;
	QPointer<snurra_library> self(this);
	m_thread = std::thread([this, folder, self] {
		QStringList piece;
		int listed = 0;
		int looked_at = 0;
		QDirIterator it(folder, QDir::Files | QDir::Readable, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
		while (it.hasNext() && !m_stop.load(std::memory_order_relaxed) && looked_at++ < LOOKED_AT_MOST && listed < LISTED_MOST) {
			const QString path = it.next();
			if (!snurra_playable_by_name(path)) continue;
			piece << path;
			++listed;
			if (piece.size() >= 500) {
				QMetaObject::invokeMethod(this, [self, piece] { if (self) self->found(piece, false); });
				piece.clear();
			}
		}
		QMetaObject::invokeMethod(this, [self, piece] { if (self) self->found(piece, true); });
	});
}

void snurra_library::stop_scan() {
	m_stop = true;
	if (m_thread.joinable()) m_thread.join();
	m_scanning = false;
}

void snurra_library::found(const QStringList &paths, bool last) {
	const QDir root(m_folder);
	m_filter->setDynamicSortFilter(false);
	for (const QString &path : paths) {
		const QFileInfo info(path);
		QStandardItem *name = new QStandardItem(info.completeBaseName());
		name->setData(path, PATH_ROLE);
		name->setToolTip(path);
		QString where = root.relativeFilePath(info.absolutePath());
		if (where == QLatin1String(".")) where.clear();
		m_model->appendRow({ name, new QStandardItem(where) });
	}
	m_filter->sort(0);
	m_filter->setDynamicSortFilter(true);
	if (last) m_scanning = false;
	say();
}

void snurra_library::say() {
	const QString where = QDir::toNativeSeparators(m_folder);
	if (m_scanning) {
		m_status->setText(tr("Looking in %1 ... %2 so far").arg(where).arg(m_model->rowCount()));
	} else if (m_model->rowCount() == 0) {
		m_status->setText(tr("No music in %1 -- choose another folder, or drop files on a snurra.").arg(where));
	} else {
		m_status->setText(tr("%1 of %2 in %3 -- double-click to play, drag out to place").arg(m_filter->rowCount())
		                          .arg(m_model->rowCount())
		                          .arg(where));
	}
}
