#include "divetooltipitem.h"
#include "divecartesianaxis.h"
#include "profilewidget2.h"
#include "dive.h"
#include "profile.h"
#include "membuffer.h"
#include "metrics.h"
#include <QPropertyAnimation>
#include <QGraphicsSceneMouseEvent>
#include <QPen>
#include <QBrush>
#include <QGraphicsScene>
#include <QSettings>
#include <QGraphicsView>
#include <QDebug>

#define PORT_IN_PROGRESS 1
#ifdef PORT_IN_PROGRESS
#include "display.h"
#endif

void ToolTipItem::addToolTip(const QString &toolTip, const QIcon &icon, const QPixmap *pixmap)
{
	const IconMetrics& iconMetrics = defaultIconMetrics();

	QGraphicsPixmapItem *iconItem = 0, *pixmapItem = 0;
	double yValue = title->boundingRect().height() + iconMetrics.spacing;
	Q_FOREACH (ToolTip t, toolTips) {
		yValue += t.second->boundingRect().height();
	}
	if (!icon.isNull()) {
		iconItem = new QGraphicsPixmapItem(icon.pixmap(iconMetrics.sz_small, iconMetrics.sz_small), this);
		iconItem->setPos(iconMetrics.spacing, yValue);
	} else {
		if (pixmap && !pixmap->isNull()) {
			pixmapItem = new QGraphicsPixmapItem(*pixmap, this);
			pixmapItem->setPos(iconMetrics.spacing, yValue);
		}
	}

	QGraphicsSimpleTextItem *textItem = new QGraphicsSimpleTextItem(toolTip, this);
	textItem->setPos(iconMetrics.spacing + iconMetrics.sz_small + iconMetrics.spacing, yValue);
	textItem->setBrush(QBrush(Qt::white));
	textItem->setFlag(ItemIgnoresTransformations);
	toolTips.push_back(qMakePair(iconItem, textItem));
	expand();
}

void ToolTipItem::clear()
{
	Q_FOREACH (ToolTip t, toolTips) {
		delete t.first;
		delete t.second;
	}
	toolTips.clear();
}

void ToolTipItem::setRect(const QRectF &r)
{
	// qDeleteAll(childItems());
	delete background;

	rectangle = r;
	setBrush(QBrush(Qt::white));
	setPen(QPen(Qt::black, 0.5));

	// Creates a 2pixels border
	QPainterPath border;
	border.addRoundedRect(-4, -4, rectangle.width() + 8, rectangle.height() + 10, 3, 3);
	border.addRoundedRect(-1, -1, rectangle.width() + 3, rectangle.height() + 4, 3, 3);
	setPath(border);

	QPainterPath bg;
	bg.addRoundedRect(-1, -1, rectangle.width() + 3, rectangle.height() + 4, 3, 3);

	QColor c = QColor(Qt::black);
	c.setAlpha(155);

	QGraphicsPathItem *b = new QGraphicsPathItem(bg, this);
	b->setFlag(ItemStacksBehindParent);
	b->setFlag(ItemIgnoresTransformations);
	b->setBrush(c);
	b->setPen(QPen(QBrush(Qt::transparent), 0));
	b->setZValue(-10);
	background = b;

	updateTitlePosition();
}

void ToolTipItem::collapse()
{
	int dim = defaultIconMetrics().sz_small;

	QPropertyAnimation *animation = new QPropertyAnimation(this, "rect");
	animation->setDuration(100);
	animation->setStartValue(nextRectangle);
	animation->setEndValue(QRect(0, 0, dim, dim));
	animation->start(QAbstractAnimation::DeleteWhenStopped);
	clear();

	status = COLLAPSED;
}

void ToolTipItem::expand()
{
	if (!title)
		return;

	const IconMetrics& iconMetrics = defaultIconMetrics();

	double width = 0, height = title->boundingRect().height() + iconMetrics.spacing;
	Q_FOREACH (ToolTip t, toolTips) {
		if (t.second->boundingRect().width() > width)
			width = t.second->boundingRect().width();
		height += t.second->boundingRect().height();
	}
	/*       Left padding, Icon Size,   space, right padding */
	width += iconMetrics.spacing + iconMetrics.sz_small + iconMetrics.spacing + iconMetrics.spacing;

	if (width < title->boundingRect().width() + iconMetrics.spacing * 2)
		width = title->boundingRect().width() + iconMetrics.spacing * 2;

	if (height < iconMetrics.sz_small)
		height = iconMetrics.sz_small;

	nextRectangle.setWidth(width);
	nextRectangle.setHeight(height);

	QPropertyAnimation *animation = new QPropertyAnimation(this, "rect");
	animation->setDuration(100);
	animation->setStartValue(rectangle);
	animation->setEndValue(nextRectangle);
	animation->start(QAbstractAnimation::DeleteWhenStopped);

	status = EXPANDED;
}

ToolTipItem::ToolTipItem(QGraphicsItem *parent) : QGraphicsPathItem(parent),
	background(0),
	separator(new QGraphicsLineItem(this)),
	title(new QGraphicsSimpleTextItem(tr("Information"), this)),
	status(COLLAPSED),
	timeAxis(0),
	lastTime(-1)
{
	memset(&pInfo, 0, sizeof(pInfo));

	setFlags(ItemIgnoresTransformations | ItemIsMovable | ItemClipsChildrenToShape);
	updateTitlePosition();
	setZValue(99);
}

ToolTipItem::~ToolTipItem()
{
	clear();
}

void ToolTipItem::updateTitlePosition()
{
	const IconMetrics& iconMetrics = defaultIconMetrics();
	if (rectangle.width() < title->boundingRect().width() + iconMetrics.spacing * 4) {
		QRectF newRect = rectangle;
		newRect.setWidth(title->boundingRect().width() + iconMetrics.spacing * 4);
		newRect.setHeight((newRect.height() && isExpanded()) ? newRect.height() : iconMetrics.sz_small);
		setRect(newRect);
	}

	title->setPos(boundingRect().width() / 2 - title->boundingRect().width() / 2 - 1, 0);
	title->setFlag(ItemIgnoresTransformations);
	title->setPen(QPen(Qt::white, 1));
	title->setBrush(Qt::white);

	if (toolTips.size() > 0) {
		double x1 = 3;
		double y1 = title->pos().y() + iconMetrics.spacing / 2 + title->boundingRect().height();
		double x2 = boundingRect().width() - 10;
		double y2 = y1;

		separator->setLine(x1, y1, x2, y2);
		separator->setFlag(ItemIgnoresTransformations);
		separator->setPen(QPen(Qt::white));
		separator->show();
	} else {
		separator->hide();
	}
}

bool ToolTipItem::isExpanded() const
{
	return status == EXPANDED;
}

void ToolTipItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
	persistPos();
	QGraphicsPathItem::mouseReleaseEvent(event);
	Q_FOREACH (QGraphicsItem *item, oldSelection) {
		item->setSelected(true);
	}
}

void ToolTipItem::persistPos()
{
	QSettings s;
	s.beginGroup("ProfileMap");
	s.setValue("tooltip_position", pos());
	s.endGroup();
}

void ToolTipItem::readPos()
{
	QSettings s;
	s.beginGroup("ProfileMap");
	QPointF value = s.value("tooltip_position").toPoint();
	if (!scene()->sceneRect().contains(value)) {
		value = QPointF(0, 0);
	}
	setPos(value);
}

void ToolTipItem::setPlotInfo(const plot_info &plot)
{
	pInfo = plot;
}

void ToolTipItem::setTimeAxis(DiveCartesianAxis *axis)
{
	timeAxis = axis;
}

void ToolTipItem::refresh(const QPointF &pos)
{
	int i;
	struct plot_data *entry;
	static QPixmap *tissues = new QPixmap(16,60);
	static QPainter *painter = new QPainter(tissues);
	int time = timeAxis->valueAt(pos);
	if (time == lastTime)
		return;

	lastTime = time;
	clear();
	struct membuffer mb = { 0 };

	entry = get_plot_details_new(&pInfo, time, &mb);
	if (entry) {
		tissues->fill();
		painter->setPen(QColor(0, 0, 0, 0));
		painter->setBrush(QColor(LIMENADE1));
		painter->drawRect(0, 10 + (100 - AMB_PERCENTAGE) / 2, 16, AMB_PERCENTAGE / 2);
		painter->setBrush(QColor(SPRINGWOOD1));
		painter->drawRect(0, 10, 16, (100 - AMB_PERCENTAGE) / 2);
		painter->setBrush(QColor("Red"));
		painter->drawRect(0,0,16,10);
		painter->setPen(QColor(0, 0, 0, 255));
		painter->drawLine(0, 60 - entry->gfline / 2, 16, 60 - entry->gfline / 2);
		painter->drawLine(0, 60 - AMB_PERCENTAGE * (entry->pressures.n2 + entry->pressures.he) / entry->ambpressure / 2,
				16, 60 - AMB_PERCENTAGE * (entry->pressures.n2 + entry->pressures.he) / entry->ambpressure /2);
		painter->setPen(QColor(0, 0, 0, 127));
		for (i=0; i<16; i++) {
			painter->drawLine(i, 60, i, 60 - entry->percentages[i] / 2);
		}
		addToolTip(QString::fromUtf8(mb.buffer, mb.len),QIcon(), tissues);
	}
	free_buffer(&mb);

	Q_FOREACH (QGraphicsItem *item, scene()->items(pos, Qt::IntersectsItemShape
		,Qt::DescendingOrder, scene()->views().first()->transform())) {
		if (!item->toolTip().isEmpty())
			addToolTip(item->toolTip());
	}
}

void ToolTipItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
	oldSelection = scene()->selectedItems();
	scene()->clearSelection();
	QGraphicsItem::mousePressEvent(event);
}
