/*
 * divelistview.cpp
 *
 * classes for the divelist of Subsurface
 *
 */
#include "divelistview.h"
#include "models.h"
#include "modeldelegates.h"
#include "mainwindow.h"
#include "subsurfacewebservices.h"
#include "divelogexportdialog.h"
#include "../display.h"
#include "exif.h"
#include "../file.h"
#include <QApplication>
#include <QHeaderView>
#include <QDebug>
#include <QSettings>
#include <QKeyEvent>
#include <QSortFilterProxyModel>
#include <QAction>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMenu>
#include <QFileDialog>
#include <string>
#include <iostream>
#include "../qthelper.h"

//                                #  Date  Rtg Dpth  Dur  Tmp Wght Suit  Cyl  Gas  SAC  OTU  CNS  Loc
static int defaultWidth[] =    {  70, 140, 90,  50,  50,  50,  50,  70,  50,  50,  70,  50,  50, 500};

DiveListView::DiveListView(QWidget *parent) : QTreeView(parent), mouseClickSelection(false), sortColumn(0),
	currentOrder(Qt::DescendingOrder), searchBox(this), dontEmitDiveChangedSignal(false), selectionSaved(false)
{
	setItemDelegate(new DiveListDelegate(this));
	setUniformRowHeights(true);
	setItemDelegateForColumn(DiveTripModel::RATING, new StarWidgetsDelegate(this));
	TagFilterSortModel *model = TagFilterSortModel::instance();
	model->setSortRole(DiveTripModel::SORT_ROLE);
	model->setFilterKeyColumn(-1); // filter all columns
	model->setFilterCaseSensitivity(Qt::CaseInsensitive);
	setModel(model);
	connect(model, SIGNAL(layoutChanged()), this, SLOT(fixMessyQtModelBehaviour()));

	setSortingEnabled(false);
	setContextMenuPolicy(Qt::DefaultContextMenu);
	header()->setContextMenuPolicy(Qt::ActionsContextMenu);

	const QFontMetrics metrics(defaultModelFont());
	int ht = metrics.height();
	int em = metrics.width('m');
	int zw = metrics.width('0');

	// Fixes for the layout needed for mac
#ifdef Q_OS_MAC
	header()->setMinimumHeight(ht + 10);
#endif

	// TODO FIXME we need this to get the header names
	// can we find a smarter way?
	DiveTripModel *tripModel = new DiveTripModel(this);

	// set the default width as a minimum between the hard-coded defaults,
	// the header text width and the (assumed) content width, calculated
	// based on type
	for (int col = DiveTripModel::NR; col < DiveTripModel::COLUMNS; ++col) {
		QString header_txt = tripModel->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
		int width = metrics.width(header_txt);
		int sw = 0;
		switch (col) {
		case DiveTripModel::NR:
		case DiveTripModel::DURATION:
			sw = 8*zw;
			break;
		case DiveTripModel::DATE:
			sw = 14*em;
			break;
		case DiveTripModel::RATING:
			sw = static_cast<StarWidgetsDelegate*>(itemDelegateForColumn(col))->starSize().width();
			break;
		case DiveTripModel::SUIT:
		case DiveTripModel::SAC:
			sw = 7*em;
			break;
		case DiveTripModel::LOCATION:
			sw = 50*em;
			break;
		default:
			sw = 5*em;
		}
		if (sw > width)
			width = sw;
		width += zw; // small padding
		if (width > defaultWidth[col])
			defaultWidth[col] = width;
	}
	delete tripModel;


	header()->setStretchLastSection(true);
	QAction *showSearchBox = new QAction(tr("Show search box"), this);
	showSearchBox->setShortcut(Qt::CTRL + Qt::Key_F);
	showSearchBox->setShortcutContext(Qt::WindowShortcut);
	addAction(showSearchBox);

	searchBox.installEventFilter(this);
	searchBox.hide();
//	connect(showSearchBox, SIGNAL(triggered(bool)), this, SLOT(showSearchEdit()));
//	connect(&searchBox, SIGNAL(textChanged(QString)), model, SLOT(setFilterFixedString(QString)));
}

DiveListView::~DiveListView()
{
	QSettings settings;
	settings.beginGroup("ListWidget");
	// don't set a width for the last column - location is supposed to be "the rest"
	for (int i = DiveTripModel::NR; i < DiveTripModel::COLUMNS - 1; i++) {
		if (isColumnHidden(i))
			continue;
		// we used to hardcode them all to 100 - so that might still be in the settings
		if (columnWidth(i) == 100 || columnWidth(i) == defaultWidth[i])
			settings.remove(QString("colwidth%1").arg(i));
		else
			settings.setValue(QString("colwidth%1").arg(i), columnWidth(i));
	}
	settings.remove(QString("colwidth%1").arg(DiveTripModel::COLUMNS - 1));
	settings.endGroup();
}

void DiveListView::setupUi()
{
	QSettings settings;
	static bool firstRun = true;
	if (firstRun)
		backupExpandedRows();
	settings.beginGroup("ListWidget");
	/* if no width are set, use the calculated width for each column;
	 * for that to work we need to temporarily expand all rows */
	expandAll();
	for (int i = DiveTripModel::NR; i < DiveTripModel::COLUMNS; i++) {
		if (isColumnHidden(i))
			continue;
		QVariant width = settings.value(QString("colwidth%1").arg(i));
		if (width.isValid())
			setColumnWidth(i, width.toInt());
		else
			setColumnWidth(i, defaultWidth[i]);
	}
	settings.endGroup();
	if (firstRun)
		restoreExpandedRows();
	else
		collapseAll();
	firstRun = false;
	setColumnWidth(lastVisibleColumn(), 10);
}

int DiveListView::lastVisibleColumn()
{
	int lastColumn = -1;
	for (int i = DiveTripModel::NR; i < DiveTripModel::COLUMNS; i++) {
		if (isColumnHidden(i))
			continue;
		lastColumn = i;
	}
	return lastColumn;
}

void DiveListView::backupExpandedRows()
{
	expandedRows.clear();
	for (int i = 0; i < model()->rowCount(); i++)
		if (isExpanded(model()->index(i, 0)))
			expandedRows.push_back(i);
}

void DiveListView::restoreExpandedRows()
{
	setAnimated(false);
	Q_FOREACH (const int &i, expandedRows)
		setExpanded(model()->index(i, 0), true);
	setAnimated(true);
}
void DiveListView::fixMessyQtModelBehaviour()
{
	QAbstractItemModel *m = model();
	for (int i = 0; i < model()->rowCount(); i++)
		if (m->rowCount(m->index(i, 0)) != 0)
			setFirstColumnSpanned(i, QModelIndex(), true);
}

// this only remembers dives that were selected, not trips
void DiveListView::rememberSelection()
{
	selectedDives.clear();
	QItemSelection selection = selectionModel()->selection();
	Q_FOREACH (const QModelIndex &index, selection.indexes()) {
		if (index.column() != 0) // We only care about the dives, so, let's stick to rows and discard columns.
			continue;
		struct dive *d = (struct dive *)index.data(DiveTripModel::DIVE_ROLE).value<void *>();
		if (d)
			selectedDives.insert(d->divetrip, get_divenr(d));
	}
	selectionSaved = true;
}

void DiveListView::restoreSelection()
{
	if (!selectionSaved)
		return;

	selectionSaved = false;
	dontEmitDiveChangedSignal = true;
	unselectDives();
	dontEmitDiveChangedSignal = false;
	Q_FOREACH (dive_trip_t *trip, selectedDives.keys()) {
		QList<int> divesOnTrip = getDivesInTrip(trip);
		QList<int> selectedDivesOnTrip = selectedDives.values(trip);

		// Only select trip if all of its dives were selected
		if (trip != NULL && divesOnTrip.count() == selectedDivesOnTrip.count())
			selectTrip(trip);
		selectDives(selectedDivesOnTrip);
	}
}

void DiveListView::selectTrip(dive_trip_t *trip)
{
	if (!trip)
		return;

	QSortFilterProxyModel *m = qobject_cast<QSortFilterProxyModel *>(model());
	QModelIndexList match = m->match(m->index(0, 0), DiveTripModel::TRIP_ROLE, QVariant::fromValue<void *>(trip), 2, Qt::MatchRecursive);
	QItemSelectionModel::SelectionFlags flags;
	if (!match.count())
		return;
	QModelIndex idx = match.first();
	flags = QItemSelectionModel::Select;
	flags |= QItemSelectionModel::Rows;
	selectionModel()->select(idx, flags);
	expand(idx);
}

void DiveListView::unselectDives()
{
	// make sure we don't try to redraw the dives during the selection change
	selected_dive = -1;
	amount_selected = 0;
	// clear the Qt selection
	selectionModel()->clearSelection();
	// clearSelection should emit selectionChanged() but sometimes that
	// appears not to happen
	// since we are unselecting all dives there is no need to use deselect_dive() - that
	// would only cause pointless churn
	int i;
	struct dive *dive;
	for_each_dive (i, dive) {
		dive->selected = false;
	}
}

QList<dive_trip_t *> DiveListView::selectedTrips()
{
	QList<dive_trip_t *> ret;
	Q_FOREACH (const QModelIndex &index, selectionModel()->selectedRows()) {
		dive_trip_t *trip = static_cast<dive_trip_t *>(index.data(DiveTripModel::TRIP_ROLE).value<void *>());
		if (!trip)
			continue;
		ret.push_back(trip);
	}
	return ret;
}

void DiveListView::selectDive(int i, bool scrollto, bool toggle)
{
	if (i == -1)
		return;
	QSortFilterProxyModel *m = qobject_cast<QSortFilterProxyModel *>(model());
	QModelIndexList match = m->match(m->index(0, 0), DiveTripModel::DIVE_IDX, i, 2, Qt::MatchRecursive);
	QItemSelectionModel::SelectionFlags flags;
	if (match.isEmpty())
		return;
	QModelIndex idx = match.first();
	flags = toggle ? QItemSelectionModel::Toggle : QItemSelectionModel::Select;
	flags |= QItemSelectionModel::Rows;
	selectionModel()->setCurrentIndex(idx, flags);
	if (idx.parent().isValid()) {
		setAnimated(false);
		expand(idx.parent());
		if (scrollto)
			scrollTo(idx.parent());
		setAnimated(true);
	}
	if (scrollto)
		scrollTo(idx, PositionAtCenter);
}

void DiveListView::selectDives(const QList<int> &newDiveSelection)
{
	if (!newDiveSelection.count())
		return;

	dontEmitDiveChangedSignal = true;
	// select the dives, highest index first - this way the oldest of the dives
	// becomes the selected_dive that we scroll to
	QList<int> sortedSelection = newDiveSelection;
	qSort(sortedSelection.begin(), sortedSelection.end());
	while (!sortedSelection.isEmpty())
		selectDive(sortedSelection.takeLast());

	QSortFilterProxyModel *m = qobject_cast<QSortFilterProxyModel *>(model());
	QModelIndex idx = m->match(m->index(0, 0), DiveTripModel::DIVE_IDX, selected_dive, 2, Qt::MatchRecursive).first();
	if (idx.parent().isValid())
		scrollTo(idx.parent());
	scrollTo(idx);

	// now that everything is up to date, update the widgets
	Q_EMIT currentDiveChanged(selected_dive);
	dontEmitDiveChangedSignal = false;
	return;
}

void DiveListView::showSearchEdit()
{
	searchBox.show();
	searchBox.setFocus();
}

bool DiveListView::eventFilter(QObject *, QEvent *event)
{
	if (event->type() != QEvent::KeyPress)
		return false;
	QKeyEvent *keyEv = static_cast<QKeyEvent *>(event);
	if (keyEv->key() != Qt::Key_Escape)
		return false;

	searchBox.clear();
	searchBox.hide();
	QSortFilterProxyModel *m = qobject_cast<QSortFilterProxyModel *>(model());
	m->setFilterFixedString(QString());
	return true;
}

// NOTE! This loses trip selection, because while we remember the
// dives, we don't remember the trips (see the "currentSelectedDives"
// list). I haven't figured out how to look up the trip from the
// index. TRIP_ROLE vs DIVE_ROLE?
void DiveListView::headerClicked(int i)
{
	DiveTripModel::Layout newLayout = i == (int)DiveTripModel::NR ? DiveTripModel::TREE : DiveTripModel::LIST;
	rememberSelection();
	unselectDives();
	/* No layout change? Just re-sort, and scroll to first selection, making sure all selections are expanded */
	if (currentLayout == newLayout) {
		currentOrder = (currentOrder == Qt::DescendingOrder) ? Qt::AscendingOrder : Qt::DescendingOrder;
		sortByColumn(i, currentOrder);
	} else {
		// clear the model, repopulate with new indexes.
		if (currentLayout == DiveTripModel::TREE) {
			backupExpandedRows();
		}
		reload(newLayout, false);
		currentOrder = Qt::DescendingOrder;
		sortByColumn(i, currentOrder);
		if (newLayout == DiveTripModel::TREE) {
			restoreExpandedRows();
		}
	}
	restoreSelection();
	// remember the new sort column
	sortColumn = i;
}

void DiveListView::reload(DiveTripModel::Layout layout, bool forceSort)
{
	// we want to run setupUi() once we actually are displaying something
	// in the widget
	static bool first = true;
	if (first && dive_table.nr > 0) {
		setupUi();
		first = false;
	}
	if (layout == DiveTripModel::CURRENT)
		layout = currentLayout;
	else
		currentLayout = layout;
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
	header()->setClickable(true);
#else
	header()->setSectionsClickable(true);
#endif
	connect(header(), SIGNAL(sectionPressed(int)), this, SLOT(headerClicked(int)), Qt::UniqueConnection);

	QSortFilterProxyModel *m = qobject_cast<QSortFilterProxyModel *>(model());
	QAbstractItemModel *oldModel = m->sourceModel();
	if (oldModel) {
		oldModel->deleteLater();
	}
	DiveTripModel *tripModel = new DiveTripModel(this);
	tripModel->setLayout(layout);

	m->setSourceModel(tripModel);

	if (!forceSort)
		return;

	sortByColumn(sortColumn, currentOrder);
	if (amount_selected && current_dive != NULL) {
		selectDive(selected_dive, true);
	} else {
		QModelIndex firstDiveOrTrip = m->index(0, 0);
		if (firstDiveOrTrip.isValid()) {
			if (m->index(0, 0, firstDiveOrTrip).isValid())
				setCurrentIndex(m->index(0, 0, firstDiveOrTrip));
			else
				setCurrentIndex(firstDiveOrTrip);
		}
	}
	if (selectedIndexes().count()) {
		QModelIndex curr = selectedIndexes().first();
		curr = curr.parent().isValid() ? curr.parent() : curr;
		if (!isExpanded(curr)) {
			setAnimated(false);
			expand(curr);
			scrollTo(curr);
			setAnimated(true);
		}
	}
	if (currentLayout == DiveTripModel::TREE) {
		fixMessyQtModelBehaviour();
	}
}

void DiveListView::reloadHeaderActions()
{
	// Populate the context menu of the headers that will show
	// the menu to show / hide columns.
	if (!header()->actions().size()) {
		QSettings s;
		s.beginGroup("DiveListColumnState");
		for (int i = 0; i < model()->columnCount(); i++) {
			QString title = QString("%1").arg(model()->headerData(i, Qt::Horizontal).toString());
			QString settingName = QString("showColumn%1").arg(i);
			QAction *a = new QAction(title, header());
			bool showHeaderFirstRun = !(i == DiveTripModel::MAXCNS ||
						    i == DiveTripModel::GAS ||
						    i == DiveTripModel::OTU ||
						    i == DiveTripModel::TEMPERATURE ||
						    i == DiveTripModel::TOTALWEIGHT ||
						    i == DiveTripModel::SUIT ||
						    i == DiveTripModel::CYLINDER ||
						    i == DiveTripModel::SAC);
			bool shown = s.value(settingName, showHeaderFirstRun).toBool();
			a->setCheckable(true);
			a->setChecked(shown);
			a->setProperty("index", i);
			a->setProperty("settingName", settingName);
			connect(a, SIGNAL(triggered(bool)), this, SLOT(toggleColumnVisibilityByIndex()));
			header()->addAction(a);
			setColumnHidden(i, !shown);
		}
		s.endGroup();
	} else {
		for (int i = 0; i < model()->columnCount(); i++) {
			QString title = QString("%1").arg(model()->headerData(i, Qt::Horizontal).toString());
			header()->actions()[i]->setText(title);
		}
	}
}

void DiveListView::toggleColumnVisibilityByIndex()
{
	QAction *action = qobject_cast<QAction *>(sender());
	if (!action)
		return;

	QSettings s;
	s.beginGroup("DiveListColumnState");
	s.setValue(action->property("settingName").toString(), action->isChecked());
	s.endGroup();
	s.sync();
	setColumnHidden(action->property("index").toInt(), !action->isChecked());
	setColumnWidth(lastVisibleColumn(), 10);
}

void DiveListView::currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
	if (!current.isValid())
		return;
	scrollTo(current);
}

void DiveListView::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
	QItemSelection newSelected = selected.size() ? selected : selectionModel()->selection();
	QItemSelection newDeselected = deselected;

	disconnect(selectionModel(), SIGNAL(selectionChanged(QItemSelection, QItemSelection)), this, SLOT(selectionChanged(QItemSelection, QItemSelection)));
	disconnect(selectionModel(), SIGNAL(currentChanged(QModelIndex, QModelIndex)), this, SLOT(currentChanged(QModelIndex, QModelIndex)));

	Q_FOREACH (const QModelIndex &index, newDeselected.indexes()) {
		if (index.column() != 0)
			continue;
		const QAbstractItemModel *model = index.model();
		struct dive *dive = (struct dive *)model->data(index, DiveTripModel::DIVE_ROLE).value<void *>();
		if (!dive) // it's a trip!
			deselect_dives_in_trip((dive_trip_t *)model->data(index, DiveTripModel::TRIP_ROLE).value<void *>());
		else
			deselect_dive(get_divenr(dive));
	}
	Q_FOREACH (const QModelIndex &index, newSelected.indexes()) {
		if (index.column() != 0)
			continue;

		const QAbstractItemModel *model = index.model();
		struct dive *dive = (struct dive *)model->data(index, DiveTripModel::DIVE_ROLE).value<void *>();
		if (!dive) { // it's a trip!
			if (model->rowCount(index)) {
				QItemSelection selection;
				select_dives_in_trip((dive_trip_t *)model->data(index, DiveTripModel::TRIP_ROLE).value<void *>());
				selection.select(index.child(0, 0), index.child(model->rowCount(index) - 1, 0));
				selectionModel()->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
				selectionModel()->setCurrentIndex(index, QItemSelectionModel::Select | QItemSelectionModel::NoUpdate);
				if (!isExpanded(index))
					expand(index);
			}
		} else {
			select_dive(get_divenr(dive));
		}
	}
	QTreeView::selectionChanged(selectionModel()->selection(), newDeselected);
	connect(selectionModel(), SIGNAL(selectionChanged(QItemSelection, QItemSelection)), this, SLOT(selectionChanged(QItemSelection, QItemSelection)));
	connect(selectionModel(), SIGNAL(currentChanged(QModelIndex, QModelIndex)), this, SLOT(currentChanged(QModelIndex, QModelIndex)));
	if (!dontEmitDiveChangedSignal)
		Q_EMIT currentDiveChanged(selected_dive);
}

static bool can_merge(const struct dive *a, const struct dive *b)
{
	if (!a || !b)
		return false;
	if (a->when > b->when)
		return false;
	/* Don't merge dives if there's more than half an hour between them */
	if (a->when + a->duration.seconds + 30 * 60 < b->when)
		return false;
	return true;
}

void DiveListView::mergeDives()
{
	int i;
	struct dive *dive, *maindive = NULL;

	for_each_dive (i, dive) {
		if (dive->selected) {
			if (!can_merge(maindive, dive)) {
				maindive = dive;
			} else {
				maindive = merge_two_dives(maindive, dive);
				i--; // otherwise we skip a dive in the freshly changed list
			}
		}
	}
	MainWindow::instance()->refreshDisplay();
}

void DiveListView::renumberDives()
{
	RenumberDialog::instance()->renumberOnlySelected();
	RenumberDialog::instance()->show();
}

void DiveListView::merge_trip(const QModelIndex &a, int offset)
{
	int i = a.row() + offset;
	QModelIndex b = a.sibling(i, 0);

	dive_trip_t *trip_a = (dive_trip_t *)a.data(DiveTripModel::TRIP_ROLE).value<void *>();
	dive_trip_t *trip_b = (dive_trip_t *)b.data(DiveTripModel::TRIP_ROLE).value<void *>();
	if (trip_a == trip_b || !trip_a || !trip_b)
		return;
	combine_trips(trip_a, trip_b);
	rememberSelection();
	reload(currentLayout, false);
	fixMessyQtModelBehaviour();
	restoreSelection();
	mark_divelist_changed(true);
	//TODO: emit a signal to signalize that the divelist changed?
}

void DiveListView::mergeTripAbove()
{
	merge_trip(contextMenuIndex, -1);
}

void DiveListView::mergeTripBelow()
{
	merge_trip(contextMenuIndex, +1);
}

void DiveListView::removeFromTrip()
{
	//TODO: move this to C-code.
	int i;
	struct dive *d;
	for_each_dive (i, d) {
		if (d->selected)
			remove_dive_from_trip(d, false);
	}
	rememberSelection();
	reload(currentLayout, false);
	fixMessyQtModelBehaviour();
	restoreSelection();
	mark_divelist_changed(true);
}

void DiveListView::newTripAbove()
{
	struct dive *d = (struct dive *)contextMenuIndex.data(DiveTripModel::DIVE_ROLE).value<void *>();
	if (!d) // shouldn't happen as we only are setting up this action if this is a dive
		return;
	//TODO: port to c-code.
	dive_trip_t *trip;
	int idx;
	rememberSelection();
	trip = create_and_hookup_trip_from_dive(d);
	for_each_dive (idx, d) {
		if (d->selected)
			add_dive_to_trip(d, trip);
	}
	trip->expanded = 1;
	reload(currentLayout, false);
	fixMessyQtModelBehaviour();
	mark_divelist_changed(true);
	restoreSelection();
}

void DiveListView::addToTripBelow()
{
	addToTrip(1);
}

void DiveListView::addToTripAbove()
{
	addToTrip(-1);
}

void DiveListView::addToTrip(int delta)
{
	// if there is a trip above / below, then it's a sibling at the same
	// level as this dive. So let's take a look
	struct dive *d = (struct dive *)contextMenuIndex.data(DiveTripModel::DIVE_ROLE).value<void *>();
	QModelIndex t = contextMenuIndex.sibling(contextMenuIndex.row() + delta, 0);
	dive_trip_t *trip = (dive_trip_t *)t.data(DiveTripModel::TRIP_ROLE).value<void *>();

	if (!trip || !d)
		// no dive, no trip? get me out of here
		return;

	rememberSelection();

	add_dive_to_trip(d, trip);
	if (d->selected) { // there are possibly other selected dives that we should add
		int idx;
		for_each_dive (idx, d) {
			if (d->selected)
				add_dive_to_trip(d, trip);
		}
	}
	trip->expanded = 1;
	mark_divelist_changed(true);

	reload(currentLayout, false);
	restoreSelection();
	fixMessyQtModelBehaviour();
}

void DiveListView::markDiveInvalid()
{
	int i;
	struct dive *d = (struct dive *)contextMenuIndex.data(DiveTripModel::DIVE_ROLE).value<void *>();
	if (!d)
		return;
	for_each_dive (i, d) {
		if (!d->selected)
			continue;
		//TODO: this should be done in the future
		// now mark the dive invalid... how do we do THAT?
		// d->invalid = true;
	}
	if (amount_selected == 0) {
		MainWindow::instance()->cleanUpEmpty();
	}
	mark_divelist_changed(true);
	MainWindow::instance()->refreshDisplay();
	if (prefs.display_invalid_dives == false) {
		clearSelection();
		// select top dive that isn't marked invalid
		rememberSelection();
	}
	fixMessyQtModelBehaviour();
}

void DiveListView::deleteDive()
{
	struct dive *d = (struct dive *)contextMenuIndex.data(DiveTripModel::DIVE_ROLE).value<void *>();
	if (!d)
		return;

	//TODO: port this to C-code.
	int i;
	// after a dive is deleted the ones following it move forward in the dive_table
	// so instead of using the for_each_dive macro I'm using an explicit for loop
	// to make this easier to understand
	int lastDiveNr = -1;
	for_each_dive (i, d) {
		if (!d->selected)
			continue;
		delete_single_dive(i);
		i--; // so the next dive isn't skipped... it's now #i
		lastDiveNr = i;
	}
	if (amount_selected == 0) {
		MainWindow::instance()->cleanUpEmpty();
	}
	mark_divelist_changed(true);
	MainWindow::instance()->refreshDisplay();
	if (lastDiveNr != -1) {
		clearSelection();
		selectDive(lastDiveNr);
		rememberSelection();
	}
	fixMessyQtModelBehaviour();
}

void DiveListView::testSlot()
{
	struct dive *d = (struct dive *)contextMenuIndex.data(DiveTripModel::DIVE_ROLE).value<void *>();
	if (d) {
		qDebug("testSlot called on dive #%d", d->number);
	} else {
		QModelIndex child = contextMenuIndex.child(0, 0);
		d = (struct dive *)child.data(DiveTripModel::DIVE_ROLE).value<void *>();
		if (d)
			qDebug("testSlot called on trip including dive #%d", d->number);
		else
			qDebug("testSlot called on trip with no dive");
	}
}

void DiveListView::contextMenuEvent(QContextMenuEvent *event)
{
	QAction *collapseAction = NULL;
	// let's remember where we are
	contextMenuIndex = indexAt(event->pos());
	struct dive *d = (struct dive *)contextMenuIndex.data(DiveTripModel::DIVE_ROLE).value<void *>();
	dive_trip_t *trip = (dive_trip_t *)contextMenuIndex.data(DiveTripModel::TRIP_ROLE).value<void *>();
	QMenu popup(this);
	if (currentLayout == DiveTripModel::TREE) {
		popup.addAction(tr("Expand all"), this, SLOT(expandAll()));
		popup.addAction(tr("Collapse all"), this, SLOT(collapseAll()));
		collapseAction = popup.addAction(tr("Collapse others"), this, SLOT(collapseAll()));
		if (d) {
			popup.addAction(tr("Remove dive(s) from trip"), this, SLOT(removeFromTrip()));
			popup.addAction(tr("Create new trip above"), this, SLOT(newTripAbove()));
			if (!d->divetrip) {
				struct dive *top = d;
				struct dive *bottom = d;
				if (d->selected) {
					if (currentOrder == Qt::AscendingOrder) {
						top = first_selected_dive();
						bottom = last_selected_dive();
					} else {
						top = last_selected_dive();
						bottom = first_selected_dive();
					}
				}
				if (is_trip_before_after(top, (currentOrder == Qt::AscendingOrder)))
					popup.addAction(tr("Add dive(s) to trip immediately above"), this, SLOT(addToTripAbove()));
				if (is_trip_before_after(bottom, (currentOrder == Qt::DescendingOrder)))
					popup.addAction(tr("Add dive(s) to trip immediately below"), this, SLOT(addToTripBelow()));
			}
		}
		if (trip) {
			popup.addAction(tr("Merge trip with trip above"), this, SLOT(mergeTripAbove()));
			popup.addAction(tr("Merge trip with trip below"), this, SLOT(mergeTripBelow()));
		}
	}
	if (d) {
		popup.addAction(tr("Delete dive(s)"), this, SLOT(deleteDive()));
#if 0
		popup.addAction(tr("Mark dive(s) invalid", this, SLOT(markDiveInvalid())));
#endif
	}
	if (amount_selected > 1 && consecutive_selected())
		popup.addAction(tr("Merge selected dives"), this, SLOT(mergeDives()));
	if (amount_selected >= 1) {
		popup.addAction(tr("Renumber dive(s)"), this, SLOT(renumberDives()));
		popup.addAction(tr("Shift times"), this, SLOT(shiftTimes()));
		popup.addAction(tr("Load images"), this, SLOT(loadImages()));
	}
	// "collapse all" really closes all trips,
	// "collapse" keeps the trip with the selected dive open
	QAction *actionTaken = popup.exec(event->globalPos());
	if (actionTaken == collapseAction && collapseAction) {
		this->setAnimated(false);
		selectDive(selected_dive, true);
		scrollTo(selectedIndexes().first());
		this->setAnimated(true);
	}
	event->accept();
}

void DiveListView::shiftTimes()
{
	ShiftTimesDialog::instance()->show();
}

void DiveListView::loadImages()
{
	QStringList fileNames = QFileDialog::getOpenFileNames(this, tr("Open image files"), lastUsedImageDir(), tr("Image files (*.jpg *.jpeg *.pnm *.tif *.tiff)"));
	if (fileNames.isEmpty())
		return;

	updateLastUsedImageDir(QFileInfo(fileNames[0]).dir().path());
	ShiftImageTimesDialog shiftDialog(this);
	shiftDialog.setOffset(lastImageTimeOffset());
	shiftDialog.exec();
	updateLastImageTimeOffset(shiftDialog.amount());

	Q_FOREACH (const QString &fileName, fileNames) {
		int j = 0;
		struct dive *dive;
		for_each_dive (j, dive) {
			if (!dive->selected)
				continue;
			dive_create_picture(dive, qstrdup(fileName.toUtf8().data()), shiftDialog.amount());
		}
	}

	mark_divelist_changed(true);
	copy_dive(current_dive, &displayed_dive);
	DivePictureModel::instance()->updateDivePictures();
}

QString DiveListView::lastUsedImageDir()
{
	QSettings settings;
	QString lastImageDir = QDir::homePath();

	settings.beginGroup("FileDialog");
	if (settings.contains("LastImageDir"))
		if (QDir::setCurrent(settings.value("LastImageDir").toString()))
			lastImageDir = settings.value("LastIamgeDir").toString();
	return lastImageDir;
}

void DiveListView::updateLastUsedImageDir(const QString &dir)
{
	QSettings s;
	s.beginGroup("FileDialog");
	s.setValue("LastImageDir", dir);
}

int DiveListView::lastImageTimeOffset()
{
	QSettings settings;
	int offset = 0;

	settings.beginGroup("MainWindow");
	if (settings.contains("LastImageTimeOffset"))
		offset = settings.value("LastImageTimeOffset").toInt();
	return offset;
}

void DiveListView::updateLastImageTimeOffset(const int offset)
{
	QSettings s;
	s.beginGroup("MainWindow");
	s.setValue("LastImageTimeOffset", offset);
}

void DiveListView::endSearch()
{
	searchBox.clear();
	searchBox.hide();
}
