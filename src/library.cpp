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

#include "snurra/beats.h"
#include "snurra/source.h"
#include "snurra/theme.h"

namespace {

constexpr int PATH_ROLE = Qt::UserRole + 1;   /* the full path, on the name */
constexpr int KEY_ROLE = Qt::UserRole + 2;    /* the key (beats.h), on the key */
/* The list's columns. */
enum { NAME, ARTIST, BPM, KEY, FOLDER, COLUMNS };
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
		const QString text = sourceModel()->index(row, FOLDER, parent).data().toString() + QLatin1Char('/')
		                     + sourceModel()->index(row, NAME, parent).data().toString() + QLatin1Char(' ')
		                     + sourceModel()->index(row, ARTIST, parent).data().toString();
		for (const QString &word : m_words)
			if (!text.contains(word, Qt::CaseInsensitive)) return false;
		return true;
	}
	/* By the column clicked, as a DJ's browser sorts: Name and Artist by
	 * their words, BPM by number, Key round the Camelot wheel -- so keys
	 * that mix are side by side -- a tune with none after those with one,
	 * whichever way; Folder -- the first order -- by path. Ties by path. */
	bool lessThan(const QModelIndex &left, const QModelIndex &right) const override {
		const QString a = sourceModel()->index(left.row(), NAME).data(PATH_ROLE).toString();
		const QString b = sourceModel()->index(right.row(), NAME).data(PATH_ROLE).toString();
		const int column = left.column();
		if (column == BPM || column == KEY) {
			const QString x = left.data().toString();
			const QString y = right.data().toString();
			if (x.isEmpty() != y.isEmpty()) return (sortOrder() == Qt::AscendingOrder) == y.isEmpty();
			if (column == BPM && x != y) return x.toDouble() < y.toDouble();
			const int p = snurra_key_order(left.data(KEY_ROLE).isValid() ? left.data(KEY_ROLE).toInt() : -1);
			const int q = snurra_key_order(right.data(KEY_ROLE).isValid() ? right.data(KEY_ROLE).toInt() : -1);
			if (column == KEY && p != q) return p < q;
		} else if (column == NAME || column == ARTIST) {
			const int words = m_counting.compare(left.data().toString(), right.data().toString());
			if (words != 0) return words < 0;
		}
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
	m_model = new QStandardItemModel(0, COLUMNS, this);
	m_model->setHorizontalHeaderLabels({ tr("Name"), tr("Artist"), tr("BPM"), tr("Key"), tr("Folder") });
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
	m_view->setSortingEnabled(true);
	m_view->sortByColumn(FOLDER, Qt::AscendingOrder);   /* the folders' order first */
	m_view->setDragDropMode(QAbstractItemView::DragOnly);
	/* The name takes the room, the folder a fixed share: names were cut to
	 * a dozen letters beside an empty folder column (seen 2026-11-13). */
	m_view->header()->setStretchLastSection(false);
	m_view->header()->setSectionResizeMode(NAME, QHeaderView::Stretch);
	for (int column : { ARTIST, BPM, KEY, FOLDER }) {
		m_view->header()->setSectionResizeMode(column, QHeaderView::Interactive);
		const int letters = column == BPM ? 4 : column == KEY ? 5 : 12;
		m_view->header()->resizeSection(column, fontMetrics().horizontalAdvance(QLatin1Char('M')) * letters);
	}
	m_status = new QLabel(this);
	/* Wrapped: a long folder's line otherwise set the window's least width,
	 * wider than a phone (seen 2026-11-13). */
	m_status->setWordWrap(true);
	snurra_mute(m_status);

	QHBoxLayout *top = new QHBoxLayout;
	top->addWidget(m_search, 1);
	top->addWidget(choose);
	m_find = new QPushButton(tr("Find tempos"), this);
	m_find->setObjectName(QStringLiteral("snurra-find-tempos"));
	m_find->setToolTip(tr("Work out the BPM and key of every tune listed that has none yet; kept for next time"));
	top->addWidget(m_find);
	connect(m_find, &QPushButton::clicked, this, &snurra_library::find_tempos);
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
#ifndef Q_OS_ANDROID
	/* Typing finds; on a phone the focus would raise the keyboard over
	 * half the screen before anything is asked. */
	m_search->setFocus();
#endif
	set_folder(folder.isEmpty() ? default_folder() : folder);
}

snurra_library::~snurra_library() {
	stop_tempos();
	stop_scan();
}

void snurra_library::set_folder(const QString &folder, bool remember) {
	stop_tempos();
	stop_scan();
	m_folder = QDir(folder).absolutePath();
	if (remember) QSettings(QStringLiteral("snurra"), QStringLiteral("snurra")).setValue(FOLDER_KEY, m_folder);
	m_model->removeRows(0, m_model->rowCount());
	m_items.clear();
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
		QStringList all;
		int listed = 0;
		int looked_at = 0;
		QDirIterator it(folder, QDir::Files | QDir::Readable, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
		while (it.hasNext() && !m_stop.load(std::memory_order_relaxed) && looked_at++ < LOOKED_AT_MOST && listed < LISTED_MOST) {
			const QString path = it.next();
			if (!snurra_playable_by_name(path)) continue;
			piece << path;
			all << path;
			++listed;
			if (piece.size() >= 500) {
				QMetaObject::invokeMethod(this, [self, piece] { if (self) self->found(piece, false); });
				piece.clear();
			}
		}
		QMetaObject::invokeMethod(this, [self, piece] { if (self) self->found(piece, true); });
		/* Then each file's own tags, as they are read: the list is there at
		 * once, the names and artists come after it. */
		std::vector<std::pair<QString, snurra_tags>> told;
		for (const QString &path : all) {
			if (m_stop.load(std::memory_order_relaxed)) return;
			snurra_tags tags = snurra_read_tags(path);
			/* The tempo where a deck has found it before (beats.h's cache):
			 * nothing is analysed for the list. */
			snurra_beat_grid grid;
			if (snurra_cached_beats(path, -1, 48000, grid)) {
				tags.bpm = grid.bpm();
				tags.key = grid.key;
			}
			if (tags.title.isEmpty() && tags.artist.isEmpty() && tags.bpm <= 0 && tags.key < 0) continue;
			told.emplace_back(path, tags);
			if (told.size() >= 200) {
				QMetaObject::invokeMethod(this, [self, told] { if (self) self->tagged(told); });
				told.clear();
			}
		}
		if (!told.empty()) QMetaObject::invokeMethod(this, [self, told] { if (self) self->tagged(told); });
	});
}

/* A file's title in place of its name where it has one, and its artist:
 * shown only -- no file is renamed, and an untagged file keeps its name,
 * a YouTube ID and all (the holder, 2026-11-13). */
void snurra_library::tagged(const std::vector<std::pair<QString, snurra_tags>> &told) {
	for (const auto &[path, tags] : told) {
		QStandardItem *name = m_items.value(path);
		if (!name) continue;
		if (!tags.title.isEmpty()) name->setText(tags.title);
		if (QStandardItem *artist = m_model->item(name->row(), ARTIST); artist && !tags.artist.isEmpty()) artist->setText(tags.artist);
		if (QStandardItem *bpm = m_model->item(name->row(), BPM); bpm && tags.bpm > 0) bpm->setText(QString::number(tags.bpm, 'f', 1));
		if (QStandardItem *key = m_model->item(name->row(), KEY); key && tags.key >= 0) {
			key->setText(snurra_key_name(tags.key));
			key->setData(tags.key, KEY_ROLE);
		}
	}
}

void snurra_library::find_tempos() {
	if (m_finding) return;
	QStringList wanted;
	for (int row = 0; row < m_model->rowCount(); ++row)
		if (m_model->item(row, BPM)->text().isEmpty() || m_model->item(row, KEY)->text().isEmpty())
			wanted << m_model->item(row, NAME)->data(PATH_ROLE).toString();
	if (wanted.isEmpty()) return;
	stop_tempos();
	m_finding = true;
	m_tempos_stop = false;
	m_find->setEnabled(false);
	QPointer<snurra_library> self(this);
	m_tempos = std::thread([this, wanted, self] {
		constexpr int RATE = 48000;
		auto stopped = [this] { return m_tempos_stop.load(std::memory_order_relaxed); };
		for (const QString &path : wanted) {
			if (stopped()) return;
			snurra_beat_grid grid;
			if (!snurra_cached_beats(path, -1, RATE, grid)) {
				std::unique_ptr<snurra_source> reader = snurra_open_source(path, RATE, 0);
				if (!reader || reader->length() <= 0 || reader->stand_in()) continue;
				grid = snurra_find_beats(*reader, RATE, stopped);
				if (stopped()) return;
				grid.rate = RATE;
				snurra_cache_beats(path, -1, grid);
			}
			if (!grid.valid() && grid.key < 0) continue;
			snurra_tags tags;
			tags.bpm = grid.bpm();
			tags.key = grid.key;
			std::vector<std::pair<QString, snurra_tags>> told{ { path, tags } };
			QMetaObject::invokeMethod(this, [self, told] { if (self) self->tagged(told); });
		}
		QMetaObject::invokeMethod(this, [self] {
			if (!self) return;
			self->m_finding = false;
			self->m_find->setEnabled(true);
		});
	});
}

void snurra_library::stop_tempos() {
	m_tempos_stop = true;
	if (m_tempos.joinable()) m_tempos.join();
	m_finding = false;
	if (m_find) m_find->setEnabled(true);
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
		m_items.insert(path, name);
		m_model->appendRow({ name, new QStandardItem(), new QStandardItem(), new QStandardItem(), new QStandardItem(where) });
	}
	m_filter->sort(m_filter->sortColumn() < 0 ? FOLDER : m_filter->sortColumn(), m_filter->sortOrder());
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
