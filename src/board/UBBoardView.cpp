/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * Copyright (C) 2013 Open Education Foundation
 *
 * Copyright (C) 2010-2013 Groupement d'Intérêt Public pour
 * l'Education Numérique en Afrique (GIP ENA)
 *
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */




#include "UBBoardView.h"

#include <QtGlobal>
#include <QtGui>
#include <QtXml>
#include <QListView>

#include "UBDrawingController.h"

#include "frameworks/UBGeometryUtils.h"
#include "frameworks/UBPlatformUtils.h"

#include "core/UBSettings.h"
#include "core/UBMimeData.h"
#include "core/UBApplication.h"
#include "core/UBSetting.h"
#include "core/UBPersistenceManager.h"
#include "core/UB.h"

#include "network/UBHttpGet.h"

#include "gui/UBStylusPalette.h"
#include "gui/UBRubberBand.h"
#include "gui/UBToolWidget.h"
#include "gui/UBResources.h"
#include "gui/UBMainWindow.h"
#include "gui/UBSnapIndicator.h"
#include "gui/UBDocumentThumbnailsView.h"

#include "board/UBBoardController.h"
#include "board/UBBoardPaletteManager.h"

#ifdef Q_OS_OSX
#include "core/UBApplicationController.h"
#include "desktop/UBDesktopAnnotationController.h"
#endif

#include "domain/UBGraphicsTextItem.h"
#include "domain/UBGraphicsPixmapItem.h"
#include "domain/UBGraphicsWidgetItem.h"
#include "domain/UBGraphicsPDFItem.h"
#include "domain/UBGraphicsPolygonItem.h"
#include "domain/UBItem.h"
#include "domain/UBGraphicsMediaItem.h"
#include "domain/UBGraphicsSvgItem.h"
#include "domain/UBGraphicsGroupContainerItem.h"
#include "domain/UBGraphicsStrokesGroup.h"
#include "domain/UBGraphicsItemDelegate.h"
#include "domain/UBGraphicsTextItemDelegate.h"

#include "document/UBDocumentProxy.h"

#include "tools/UBGraphicsRuler.h"
#include "tools/UBGraphicsAxes.h"
#include "tools/UBGraphicsCurtainItem.h"
#include "tools/UBGraphicsCompass.h"
#include "tools/UBGraphicsCache.h"
#include "tools/UBGraphicsTriangle.h"
#include "tools/UBGraphicsProtractor.h"

#include "core/memcheck.h"

UBBoardView::UBBoardView (UBBoardController* pController, QWidget* pParent, bool isControl, bool isDesktop)
    : QGraphicsView (pParent)
    , mController (pController)
    , mIsCreatingTextZone (false)
    , mIsCreatingSceneGrabZone (false)
    , mOkOnWidget(false)
    , _movingItem(nullptr)
    , suspendedMousePressEvent(NULL)
    , mLongPressInterval(350)
    , mIsDragInProgress(false)
    , mMultipleSelectionIsEnabled(false)
    , bIsControl(isControl)
    , bIsDesktop(isDesktop)
{
    init ();

    mFilterZIndex = false;
    /*
    mFilterZIndex = true;
    mStartLayer = UBItemLayerType::FixedBackground;
    mEndLayer = UBItemLayerType::Control;
    */


    mLongPressTimer.setInterval(mLongPressInterval);
    mLongPressTimer.setSingleShot(true);
}

UBBoardView::UBBoardView (UBBoardController* pController, int pStartLayer, int pEndLayer, QWidget* pParent, bool isControl, bool isDesktop)
    : QGraphicsView (pParent)
    , mController (pController)
    , _movingItem(nullptr)
    , suspendedMousePressEvent(NULL)
    , mLongPressInterval(350)
    , mIsDragInProgress(false)
    , mMultipleSelectionIsEnabled(false)
    , bIsControl(isControl)
    , bIsDesktop(isDesktop)
{
    init ();

    mStartLayer = pStartLayer;
    mEndLayer = pEndLayer;

    mFilterZIndex = true;

    mLongPressTimer.setInterval(mLongPressInterval);
    mLongPressTimer.setSingleShot(true);
}

UBBoardView::~UBBoardView ()
{
    if (suspendedMousePressEvent){
        delete suspendedMousePressEvent;
        suspendedMousePressEvent = NULL;
    }
}

void UBBoardView::init ()
{
    connect (UBSettings::settings ()->boardPenPressureSensitive, SIGNAL (changed (QVariant)),
             this, SLOT (settingChanged (QVariant)));

    connect (UBSettings::settings ()->boardMarkerPressureSensitive, SIGNAL (changed (QVariant)),
             this, SLOT (settingChanged (QVariant)));

    connect (UBSettings::settings ()->boardUseHighResTabletEvent, SIGNAL (changed (QVariant)),
             this, SLOT (settingChanged (QVariant)));

    connect(mController, &UBBoardController::controlViewportChanged, this, [this](){
        if (scene())
        {
            scene()->controlViewportChanged();
        }
    });

    setOptimizationFlags (QGraphicsView::IndirectPainting | QGraphicsView::DontSavePainterState); // enable UBBoardView::drawItems filter
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setWindowFlags (Qt::FramelessWindowHint);
    setFrameStyle (QFrame::NoFrame);
    setRenderHints (QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    setAcceptDrops (true);

    mTabletStylusIsPressed = false;
    mMouseButtonIsPressed = false;
    mPendingStylusReleaseEvent = false;

    setCacheMode (QGraphicsView::CacheBackground);

    mUsingTabletEraser = false;
    mIsCreatingTextZone = false;
    mRubberBand = 0;
    mUBRubberBand = 0;

    mVirtualKeyboardActive = false;

    settingChanged (QVariant ());

    unsetCursor();

    setMovingItem(NULL);
    mWidgetMoved = false;
}

std::shared_ptr<UBGraphicsScene> UBBoardView::scene ()
{
    auto currentScene = dynamic_cast<UBGraphicsScene*>(QGraphicsView::scene());
    return currentScene ? currentScene->shared_from_this() : nullptr;
}


void UBBoardView::keyPressEvent (QKeyEvent *event)
{
    // send to the scene anyway
    QApplication::sendEvent (scene().get(), event);

    if (!event->isAccepted ())
    {
        //https://doc.qt.io/qt-6/qt.html#KeyboardModifier-enum
        // on macOS, Qt::KeypadModifier is set for arrow keys
        if (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier)
        {
            switch (event->key ())
            {
            case Qt::Key_Up:
            case Qt::Key_PageUp:
            case Qt::Key_Left:
            {
                mController->previousScene ();
                break;
            }

            case Qt::Key_Down:
            case Qt::Key_PageDown:
            case Qt::Key_Right:
            case Qt::Key_Space:
            {
                mController->nextScene ();
                break;
            }

            case Qt::Key_Home:
            {
                mController->firstScene ();
                break;
            }
            case Qt::Key_End:
            {
                mController->lastScene ();
                break;
            }
            case Qt::Key_Insert:
            {
                mController->addScene ();
                break;
            }
            }
        }
        else if (event->modifiers () & Qt::ControlModifier) // keep only ctrl/cmd keys
        {
            switch (event->key ())
            {
            case Qt::Key_Plus:
            {
                mController->zoomIn ();
                event->accept ();
                break;
            }
            case Qt::Key_Minus:
            {
                mController->zoomOut ();
                event->accept ();
                break;
            }
            case Qt::Key_0:
            {
                mController->zoomRestore ();
                event->accept ();
                break;
            }
            case Qt::Key_Left:
            {
                mController->handScroll (-100, 0);
                event->accept ();
                break;
            }
            case Qt::Key_Right:
            {
                mController->handScroll (100, 0);
                event->accept ();
                break;
            }
            case Qt::Key_Up:
            {
                mController->handScroll (0, -100);
                event->accept ();
                break;
            }
            case Qt::Key_Down:
            {
                mController->handScroll (0, 100);
                event->accept ();
                break;
            }
            default:
            {
                // NOOP
            }
            }
        }
    }
}


bool UBBoardView::event (QEvent * e)
{
    if (e->type () == QEvent::Gesture)
    {
        QGestureEvent *gestureEvent = dynamic_cast<QGestureEvent *> (e);
        if (gestureEvent)
        {
            QSwipeGesture* swipe = dynamic_cast<QSwipeGesture*> (gestureEvent->gesture (Qt::SwipeGesture));
            if (swipe)
            {
                if (swipe->horizontalDirection () == QSwipeGesture::Left)
                {
                    mController->previousScene ();
                    gestureEvent->setAccepted (swipe, true);
                }

                if (swipe->horizontalDirection () == QSwipeGesture::Right)
                {
                    mController->nextScene ();
                    gestureEvent->setAccepted (swipe, true);
                }
            }
        }
    }

    return QGraphicsView::event (e);
}

void UBBoardView::tabletEvent (QTabletEvent * event)
{
    if (!mUseHighResTabletEvent) {
        event->setAccepted (false);
        return;
    }

    UBDrawingController *dc = UBDrawingController::drawingController ();

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QPointF tabletPos = event->position();
#else
    QPointF tabletPos = event->posF();
#endif
    UBStylusTool::Enum currentTool = (UBStylusTool::Enum)dc->stylusTool ();

    if (event->type () == QEvent::TabletPress || event->type () == QEvent::TabletEnterProximity) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        if (event->pointerType () == QPointingDevice::PointerType::Eraser) {
#else
        if (event->pointerType () == QTabletEvent::Eraser) {
#endif
            dc->setStylusTool (UBStylusTool::Eraser);
            mUsingTabletEraser = true;
        }
        else {
            if (mUsingTabletEraser && currentTool == UBStylusTool::Eraser)
                dc->setStylusTool (dc->latestDrawingTool ());

            mUsingTabletEraser = false;
        }
    }

    QPointF scenePos = viewportTransform ().inverted ().map (tabletPos);

    qreal pressure = 1.0;
    if (((currentTool == UBStylusTool::Pen || currentTool == UBStylusTool::Line) && mPenPressureSensitive) ||
            (currentTool == UBStylusTool::Marker && mMarkerPressureSensitive))
        pressure = event->pressure ();
    else{
        //Explanation: rerouting to mouse event
        event->setAccepted (false);
        return;
    }

    bool acceptEvent = true;
#ifdef Q_OS_OSX
    //Work around #1388. After selecting annotation tool in desktop mode, annotation view appears on top when
    //using Mac OS X. In this case tablet event should send mouse event so as to let user interact with
    //stylus palette.
    Q_ASSERT(UBApplication::applicationController->uninotesController());
    if (UBApplication::applicationController->uninotesController()->drawingView() == this) {
        if (UBApplication::applicationController->uninotesController()->desktopPalettePath().contains(event->pos())) {
            acceptEvent = false;
        }
    }
#endif

    switch (event->type ()) {
    case QEvent::TabletPress: {
        mTabletStylusIsPressed = true;
        scene()->inputDevicePress (scenePos, pressure, event->modifiers());

        break;
    }
    case QEvent::TabletMove: {
        if (mTabletStylusIsPressed)
            scene ()->inputDeviceMove (scenePos, pressure, event->modifiers());

        acceptEvent = false; // rerouted to mouse move

        break;

    }
    case QEvent::TabletRelease: {
        UBStylusTool::Enum currentTool = (UBStylusTool::Enum)dc->stylusTool ();
        scene ()->setToolCursor (currentTool);
        setToolCursor (currentTool);

        scene ()->inputDeviceRelease (currentTool, event->modifiers());

        mPendingStylusReleaseEvent = false;

        mTabletStylusIsPressed = false;
        mMouseButtonIsPressed = false;

        break;
    }
    default: {
        //NOOP - avoid compiler warning
    }
    }

    // ignore mouse press and mouse move tablet event so that it is rerouted to mouse events,
    // documented in QTabletEvent Class Reference:
    /* The event handler QWidget::tabletEvent() receives all three types of tablet events.
     Qt will first send a tabletEvent then, if it is not accepted, it will send a mouse event. */
    //
    // This is a workaround to the fact that tablet event are not delivered to child widget (like palettes)
    //

    event->setAccepted (acceptEvent);

}

bool UBBoardView::itemIsLocked(QGraphicsItem *item)
{
    if (!item)
        return false;

    return item->data(UBGraphicsItemData::ItemLocked).toBool();
}

bool UBBoardView::itemHaveParentWithType(QGraphicsItem *item, int type)
{
    if (!item)
        return false;

    if (type == item->type())
        return true;

    return itemHaveParentWithType(item->parentItem(), type);

}

bool UBBoardView::isUBItem(QGraphicsItem *item)
{
    if ((UBGraphicsItemType::UserTypesCount > item->type()) && (item->type() > QGraphicsItem::UserType))
        return true;

    return false;
}

bool UBBoardView::isCppTool(QGraphicsItem *item)
{
    return (item->type() == UBGraphicsItemType::CompassItemType
            || item->type() == UBGraphicsItemType::RulerItemType
            || item->type() == UBGraphicsItemType::AxesItemType
            || item->type() == UBGraphicsItemType::ProtractorItemType
            || item->type() == UBGraphicsItemType::TriangleItemType
            || item->type() == UBGraphicsItemType::CurtainItemType);
}

void UBBoardView::handleItemsSelection(QGraphicsItem *item)
{
    // we need to select new pressed itemOnBoard and deselect all other items.
    // the trouble is in:
    //                  some items can has parents (groupped items or strokes, or strokes in groups).
    //                  some items is already selected and we don't need to reselect them
    //
    // item selection managed by QGraphicsView::mousePressEvent(). It should be called later.

    if (item)
    {
        //  item has group as first parent - it is any item or UBGraphicsStrokesGroup.
        if (getMovingItem())
        {
            if (getMovingItem()->parentItem())
            {
                if(item->parentItem() && UBGraphicsGroupContainerItem::Type == getMovingItem()->parentItem()->type())
                    return;
            }
        }

        // delegate buttons shouldn't selected
        if (DelegateButton::Type == item->type())
            return;

        // click on svg items (images on Frame) shouldn't change selection.
        if (QGraphicsSvgItem::Type == item->type())
            return;

        // Delegate frame shouldn't selected
        if (UBGraphicsDelegateFrame::Type == item->type())
            return;


        // if we need to uwe multiple selection - we shouldn't deselect other items.
        if (!isMultipleSelectionEnabled())
        {
            // here we need to determine what item is pressed. We should work
            // only with UB items.
            if ((UBGraphicsItemType::UserTypesCount > item->type()) && (item->type() > QGraphicsItem::UserType))
            {
                scene()->deselectAllItemsExcept(item);
                scene()->updateSelectionFrame();
            }
        }
    }
}

bool UBBoardView::itemShouldReceiveMousePressEvent(QGraphicsItem *item)
{
    /*
Some items should receive mouse press events averytime,
some items should receive that events when they are selected,
some items shouldn't receive mouse press events at mouse press, but should receive them at mouse release (suspended mouse press event)

Here we determines cases when items should to get mouse press event at pressing on mouse.
*/

    if (!item)
        return true;

    // for now background objects is not interactable, but it can be deprecated for some items in the future.
    if (item == scene()->backgroundObject())
        return false;

    // some behavior depends on current tool.
    UBStylusTool::Enum currentTool = (UBStylusTool::Enum)UBDrawingController::drawingController()->stylusTool();

    switch(item->type())
    {
    case UBGraphicsProtractor::Type:
    case UBGraphicsRuler::Type:
    case UBGraphicsAxes::Type:
    case UBGraphicsTriangle::Type:
    case UBGraphicsCompass::Type:
    case UBGraphicsCache::Type:
        return true;
    case UBGraphicsDelegateFrame::Type:
        if (currentTool == UBStylusTool::Play)
            return false;
        return true;
    case UBGraphicsPixmapItem::Type:
    case UBGraphicsSvgItem::Type:
        if (currentTool == UBStylusTool::Play)
            return true;
        if (item->isSelected())
            return true;
        else
            return false;
    case DelegateButton::Type:
        return true;

    case UBGraphicsMediaItem::Type:
    case UBGraphicsVideoItem::Type:
    case UBGraphicsAudioItem::Type:
        return false;

    case UBGraphicsTextItem::Type:
        if (currentTool == UBStylusTool::Play)
            return true;
        if ((currentTool == UBStylusTool::Selector) && item->isSelected())
            return true;
        if ((currentTool == UBStylusTool::Selector) && item->parentItem() && item->parentItem()->isSelected())
            return true;
        if (currentTool != UBStylusTool::Selector)
            return false;
        break;

    case UBGraphicsItemType::StrokeItemType:
        if (currentTool == UBStylusTool::Play || currentTool == UBStylusTool::Selector)
            return true;
        break;
    // Groups shouldn't reacts on any presses and moves for Play tool.
    case UBGraphicsGroupContainerItem::Type:
        if(currentTool == UBStylusTool::Play)
        {
            setMovingItem(NULL);
            return true;
        }
        return false;
        break;
    case QGraphicsProxyWidget::Type:
        return true;

    case UBGraphicsWidgetItem::Type:
        if (currentTool == UBStylusTool::Selector && item->parentItem() && item->parentItem()->isSelected())
            return true;
        if (currentTool == UBStylusTool::Selector && item->isSelected())
            return true;
        if (currentTool == UBStylusTool::Play)
            return true;
        return false;
        break;
    }

    return !isUBItem(item); // standard behavior of QGraphicsScene for not UB items. UB items should be managed upper.
}

bool UBBoardView::itemShouldReceiveSuspendedMousePressEvent(QGraphicsItem *item)
{
    if (!item)
        return false;

    if (item == scene()->backgroundObject())
        return false;

    UBStylusTool::Enum currentTool = (UBStylusTool::Enum)UBDrawingController::drawingController()->stylusTool();

    switch(item->type())
    {
    case UBGraphicsPixmapItem::Type:
    case UBGraphicsSvgItem::Type:
    case UBGraphicsTextItem::Type:
    case UBGraphicsWidgetItem::Type:
        if (currentTool == UBStylusTool::Selector && !item->isSelected() && item->parentItem())
            return true;
        if (currentTool == UBStylusTool::Selector && item->isSelected())
            return true;
        break;

    case DelegateButton::Type:
    case UBGraphicsMediaItem::Type:
    case UBGraphicsVideoItem::Type:
    case UBGraphicsAudioItem::Type:
        return true;
    }

    return false;

}

bool UBBoardView::itemShouldBeMoved(QGraphicsItem *item)
{
    if (!item)
        return false;

    if (item == scene()->backgroundObject())
        return false;

    if (!(mMouseButtonIsPressed || mTabletStylusIsPressed))
        return false;

    if (getMovingItem())
    {
        if (getMovingItem()->data(UBGraphicsItemData::ItemLocked).toBool())
            return false;

        if (getMovingItem()->parentItem())
        {
            if (UBGraphicsGroupContainerItem::Type == getMovingItem()->parentItem()->type() && !getMovingItem()->isSelected() && getMovingItem()->parentItem()->isSelected())
                return false;
        }
    }

    UBStylusTool::Enum currentTool = (UBStylusTool::Enum)UBDrawingController::drawingController()->stylusTool();

    switch(item->type())
    {
    case UBGraphicsCurtainItem::Type:
    case UBGraphicsGroupContainerItem::Type:
        return true;

    case UBGraphicsWidgetItem::Type:
        if(currentTool == UBStylusTool::Selector && item->isSelected())
            return false;
        if(currentTool == UBStylusTool::Play)
            return false;
        Q_FALLTHROUGH();

    case UBGraphicsSvgItem::Type:
    case UBGraphicsPixmapItem::Type:
        if (currentTool == UBStylusTool::Play || !item->isSelected())
            return true;
        if (item->isSelected())
            return false;
        Q_FALLTHROUGH();

    case UBGraphicsMediaItem::Type:
    case UBGraphicsVideoItem::Type:
    case UBGraphicsAudioItem::Type:
    case UBGraphicsStrokesGroup::Type:
        return true;

    case UBGraphicsTextItem::Type:
        if (currentTool == UBStylusTool::Play)
            return true;
        else
            return !item->isSelected();
    }

    return false;
}


QGraphicsItem* UBBoardView::determineItemToPress(QGraphicsItem *item)
{
    if(item)
    {
        UBStylusTool::Enum currentTool = (UBStylusTool::Enum)UBDrawingController::drawingController()->stylusTool();

        // if item is on group and group is not selected - group should take press.
        if (UBStylusTool::Selector == currentTool
                && item->parentItem()
                && UBGraphicsGroupContainerItem::Type == item->parentItem()->type()
                && !item->parentItem()->isSelected())
            return item->parentItem();

        // items like polygons placed in two groups nested, so we need to recursive call.
        if(item->parentItem() && UBGraphicsStrokesGroup::Type == item->parentItem()->type())
            return determineItemToPress(item->parentItem());
    }

    return item;
}

// determine item to interacts: item self or it's container.
QGraphicsItem* UBBoardView::determineItemToMove(QGraphicsItem *item)
{
    if(item)
    {
        UBStylusTool::Enum currentTool = (UBStylusTool::Enum)UBDrawingController::drawingController()->stylusTool();

        //W3C widgets should take mouse move events from play tool.
        if ((UBStylusTool::Play == currentTool) && (UBGraphicsWidgetItem::Type == item->type()))
            return item;

        // if item is in group
        if(item->parentItem() && UBGraphicsGroupContainerItem::Type == item->parentItem()->type())
        {
            // play tool should move groups by any element
            if (UBStylusTool::Play == currentTool && item->parentItem()->isSelected())
                return item->parentItem();

            // groups should should be moved instead of strokes groups
            if (UBGraphicsStrokesGroup::Type == item->type())
                return item->parentItem();

            // selected groups should be moved by moving any element
            if (item->parentItem()->isSelected())
                return item;

            if (item->isSelected())
                return NULL;

            return item->parentItem();
        }

        // items like polygons placed in two groups nested, so we need to recursive call.
        if(item->parentItem() && UBGraphicsStrokesGroup::Type == item->parentItem()->type())
            return determineItemToMove(item->parentItem());
    }

    return item;
}

void UBBoardView::handleItemMousePress(QMouseEvent *event)
{
    mLastPressedMousePos = mapToScene(event->pos());

    // Determining item who will take mouse press event
    //all other items will be deselected and if all item will be deselected, then
    // wrong item can catch mouse press. because selected items placed on the top
    setMovingItem(determineItemToPress(getMovingItem()));
    handleItemsSelection(getMovingItem());

    if (isMultipleSelectionEnabled())
        return;

    if (itemShouldReceiveMousePressEvent(getMovingItem())){
        QGraphicsView::mousePressEvent (event);

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        QPointF eventPosition = event->position();
#else
        QPointF eventPosition = event->localPos();
#endif
        QGraphicsItem* item = determineItemToPress(scene()->itemAt(this->mapToScene(eventPosition.toPoint()), transform()));
        //use QGraphicsView::transform() to use not deprecated QGraphicsScene::itemAt() method

        // NOTE @letsfindaway obsolete, probably from UBThumbnailProxyWidget
        if (item && (item->type() == QGraphicsProxyWidget::Type) && item->parentObject() && item->parentObject()->type() != QGraphicsProxyWidget::Type)
        {
            //Clean up children
            QList<QGraphicsItem*> children = item->childItems();

            for( QList<QGraphicsItem*>::iterator it = children.begin(); it != children.end(); ++it )
                if ((*it)->pos().x() < 0 || (*it)->pos().y() < 0)
                    (*it)->setPos(0,item->boundingRect().size().height());
        }
    }
    else
    {
        if (getMovingItem())
        {
            UBGraphicsItem *graphicsItem = dynamic_cast<UBGraphicsItem*>(getMovingItem());
            if (graphicsItem)
                graphicsItem->Delegate()->startUndoStep();

            getMovingItem()->clearFocus();
        }

        if (suspendedMousePressEvent)
        {
            delete suspendedMousePressEvent;
            suspendedMousePressEvent = NULL;
        }

        if (itemShouldReceiveSuspendedMousePressEvent(getMovingItem()))
        {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 4, 0))
            suspendedMousePressEvent = new QMouseEvent(event->type(), event->position(), event->globalPosition(), event->button(), event->buttons(), event->modifiers());
#elif (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
            suspendedMousePressEvent = new QMouseEvent(event->type(), event->position(), event->button(), event->buttons(), event->modifiers());
#else
            suspendedMousePressEvent = new QMouseEvent(event->type(), event->pos(), event->button(), event->buttons(), event->modifiers());
#endif
        }
    }
}

void UBBoardView::handleItemMouseMove(QMouseEvent *event)
{
    // determine item to move (maybee we need to move group of item or his parent.
    setMovingItem(determineItemToMove(getMovingItem()));

    // items should be moved not every mouse move.
    if (getMovingItem() && itemShouldBeMoved(getMovingItem()) && (mMouseButtonIsPressed || mTabletStylusIsPressed))
    {
        QPointF scenePos = mapToScene(event->pos());
        auto movingItem = getMovingItem();
        QPointF newPos = movingItem->pos() + scenePos - mLastPressedMousePos;
        movingItem->setPos(newPos);

        // snap to grid
        if (scene()->isSnapping())
        {
            QRectF rect = UBGraphicsScene::itemRect(movingItem);
            Qt::Corner corner;
            auto offset = scene()->snap(rect, &corner);
            newPos += offset;
            movingItem->setPos(newPos);

            mLastPressedMousePos = scenePos + offset;
        }
        else
        {
            mLastPressedMousePos = scenePos;
        }

        mWidgetMoved = true;
        event->accept();
    }
    else
    {
        QPointF posBeforeMove;
        QPointF posAfterMove;

        if (getMovingItem())
        {
            posBeforeMove = getMovingItem()->pos();
            QGraphicsView::mouseMoveEvent (event);
            // At the end of a d'n'd, QGraphicsView::mouseMoveEvent triggers dropEvent, setting moving item to null
            // so we must check movingItem again
            if (getMovingItem())
                posAfterMove = getMovingItem()->pos();
        }
        else
        {
            if (!mMouseButtonIsPressed)
            {
                QGraphicsView::mouseMoveEvent(event);
            }
        }

        mWidgetMoved = ((posAfterMove-posBeforeMove).manhattanLength() != 0);

        // a cludge for terminate moving of w3c widgets.
        // in some cases w3c widgets catches mouse move and doesn't sends that events to web page,
        // at simple - in google map widget - mouse move events doesn't comes to web page from rectangle of wearch bar on bottom right corner of widget.
        if (getMovingItem())
        {
            if (mWidgetMoved && UBGraphicsW3CWidgetItem::Type == getMovingItem()->type())
                getMovingItem()->setPos(posBeforeMove);
        }
    }
}

void UBBoardView::rubberItems()
{
    if (mUBRubberBand)
        mRubberedItems = items(mUBRubberBand->geometry());

    foreach(QGraphicsItem *item, mRubberedItems)
    {
        if (item->parentItem() && UBGraphicsGroupContainerItem::Type == item->parentItem()->type())
            mRubberedItems.removeOne(item);
    }
}

void UBBoardView::moveRubberedItems(QPointF movingVector)
{
    QRectF invalidateRect = scene()->itemsBoundingRect();

    foreach (QGraphicsItem *item, mRubberedItems)
    {

        if (item->type() == UBGraphicsW3CWidgetItem::Type
                || item->type() == UBGraphicsPixmapItem::Type
                || item->type() == UBGraphicsMediaItem::Type
                || item->type() == UBGraphicsVideoItem::Type
                || item->type() == UBGraphicsAudioItem::Type
                || item->type() == UBGraphicsSvgItem::Type
                || item->type() == UBGraphicsTextItem::Type
                || item->type() == UBGraphicsStrokesGroup::Type
                || item->type() == UBGraphicsGroupContainerItem::Type)
        {
            item->setPos(item->pos()+movingVector);
        }
    }

    scene()->invalidate(invalidateRect);
}

void UBBoardView::setMultiselection(bool enable)
{
    mMultipleSelectionIsEnabled = enable;
}

void UBBoardView::updateSnapIndicator(Qt::Corner corner, QPointF snapPoint)
{
    if (!mSnapIndicator)
    {
        mSnapIndicator = new UBSnapIndicator(this);
        mSnapIndicator->resize(120, 120);
    }

    mSnapIndicator->appear(corner, snapPoint);
}

void UBBoardView::setBoxing(const QMargins& margins)
{
    mMargins = margins;
}

// work around for handling tablet events on MAC OS with Qt 4.8.0 and above
#if defined(Q_OS_OSX)
bool UBBoardView::directTabletEvent(QEvent *event)
{
    QTabletEvent *tEvent = static_cast<QTabletEvent *>(event);
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    const QPointingDevice *device = dynamic_cast<const QPointingDevice*>(tEvent->device());
    tEvent = new QTabletEvent(tEvent->type()
                              , device
                              , mapFromGlobal(tEvent->pos())
                              , tEvent->globalPos()
                              , tEvent->pressure()
                              , tEvent->xTilt()
                              , tEvent->yTilt()
                              , tEvent->tangentialPressure()
                              , tEvent->rotation()
                              , tEvent->z()
                              , tEvent->modifiers()
                              , tEvent->button()
                              , tEvent->buttons());
#else
    tEvent = new QTabletEvent(tEvent->type()
                              , mapFromGlobal(tEvent->pos())
                              , tEvent->globalPos()
                              , tEvent->device()
                              , tEvent->pointerType()
                              , tEvent->pressure()
                              , tEvent->xTilt()
                              , tEvent->yTilt()
                              , tEvent->tangentialPressure()
                              , tEvent->rotation()
                              , tEvent->z()
                              , tEvent->modifiers()
                              , tEvent->uniqueId());
#endif

    if (geometry().contains(tEvent->pos()))
    {
        if (NULL == widgetForTabletEvent(this->parentWidget(), tEvent->pos()))
        {
            tabletEvent(tEvent);
            return true;
        }
    }
    return false;
}

QWidget *UBBoardView::widgetForTabletEvent(QWidget *w, const QPoint &pos)
{
    Q_ASSERT(w);

    // it should work that, but it doesn't. So we check if it is control view.
    //UBBoardView *board = qobject_cast<UBBoardView *>(w);
    UBBoardView *board = UBApplication::boardController->controlView();

    QWidget *childAtPos = NULL;

    QList<QObject *> childs = w->children();
    foreach(QObject *child, childs)
    {
        QWidget *childWidget = qobject_cast<QWidget *>(child);
        if (childWidget)
        {
            if (childWidget->isVisible() && childWidget->geometry().contains(pos))
            {
                QWidget *lastChild = widgetForTabletEvent(childWidget, pos);

                if (board && board->viewport() == lastChild)
                    continue;

                if (NULL != lastChild)
                    childAtPos = lastChild;
                else
                    childAtPos = childWidget;

                break;
            }
            else
                childAtPos = NULL;
        }
    }
    return childAtPos;
}
#endif

void UBBoardView::longPressEvent()
{
    UBDrawingController *drawingController = UBDrawingController::drawingController();
    UBStylusTool::Enum currentTool = (UBStylusTool::Enum)UBDrawingController::drawingController ()->stylusTool ();

    disconnect(&mLongPressTimer, SIGNAL(timeout()), this, SLOT(longPressEvent()));

    if (UBStylusTool::Selector == currentTool)
    {
        drawingController->setStylusTool(UBStylusTool::Play);
    }
    else
        if (currentTool == UBStylusTool::Play)
        {
            drawingController->setStylusTool(UBStylusTool::Selector);
        }
        else
            if (UBStylusTool::Eraser == currentTool)
            {
                UBApplication::boardController->paletteManager()->toggleErasePalette(true);
            }

}

void UBBoardView::mousePressEvent (QMouseEvent *event)
{
    if (!bIsControl && !bIsDesktop) {
        event->ignore();
        return;
    }

    mIsDragInProgress = false;

    if (isAbsurdPoint (event->pos ())) {
        event->accept ();
        return;
    }

    setMultiselection(event->modifiers() & Qt::ControlModifier);

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QPointF eventPosition = event->position();
#else
    QPointF eventPosition = event->localPos();
#endif
    mMouseDownPos = eventPosition.toPoint();

    setMovingItem(scene()->itemAt(this->mapToScene(eventPosition.toPoint()), QTransform()));

    if (event->button () == Qt::LeftButton && isInteractive())
    {
        int currentTool = (UBStylusTool::Enum)UBDrawingController::drawingController ()->stylusTool ();
        if (!mTabletStylusIsPressed)
            mMouseButtonIsPressed = true;

        switch (currentTool) {
        case UBStylusTool::ZoomIn :
            mController->zoomIn (mapToScene (event->pos ()));
            event->accept();
            break;

        case UBStylusTool::ZoomOut :
            mController->zoomOut (mapToScene (event->pos ()));
            event->accept();
            break;

        case UBStylusTool::Hand :
            viewport()->setCursor(QCursor (Qt::ClosedHandCursor));
            mPreviousPoint = eventPosition;
            event->accept();
            break;

        case UBStylusTool::Selector :
        case UBStylusTool::Play :
            if (bIsDesktop) {
                event->ignore();
                return;
            }

            if (scene()->backgroundObject() == getMovingItem())
                setMovingItem(NULL);

            connect(&mLongPressTimer, SIGNAL(timeout()), this, SLOT(longPressEvent()));
            if (!getMovingItem() && !mController->cacheIsVisible())
                mLongPressTimer.start();

            handleItemMousePress(event);
            event->accept();
            break;

        case UBStylusTool::Text : {
            if (dynamic_cast<UBGraphicsTextItem*>(getMovingItem()))
            {
                mIsCreatingTextZone = false;
                UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);
                QGraphicsView::mousePressEvent (event);
            }
            else
            {
                scene()->deselectAllItems();

                if (!mRubberBand)
                    mRubberBand = new UBRubberBand (QRubberBand::Rectangle, this);
                mRubberBand->setGeometry (QRect (mMouseDownPos, QSize ()));
                mRubberBand->show();
                mIsCreatingTextZone = true;

                event->accept ();
            }
        } break;

        case UBStylusTool::Capture :
            scene ()->deselectAllItems ();

            if (!mRubberBand)
                mRubberBand = new UBRubberBand (QRubberBand::Rectangle, this);

            mRubberBand->setGeometry (QRect (mMouseDownPos, QSize ()));
            mRubberBand->show ();
            mIsCreatingSceneGrabZone = true;

            event->accept ();
            break;

        default:
            if (UBDrawingController::drawingController()->activeRuler() == nullptr) {
                viewport()->setCursor (QCursor (Qt::BlankCursor));
            }
            if (scene () && !mTabletStylusIsPressed) {
                if (currentTool == UBStylusTool::Eraser) {
                    connect(&mLongPressTimer, SIGNAL(timeout()), this, SLOT(longPressEvent()));
                    mLongPressTimer.start();
                }
                scene()->inputDevicePress(mapToScene(UBGeometryUtils::pointConstrainedInRect(event->pos(), rect())), 1., event->modifiers());
            }
            event->accept ();
        }
    }
    else if (event->button () == Qt::RightButton && isInteractive())
    {
        // forward right-click events to items
        int currentTool = (UBStylusTool::Enum)UBDrawingController::drawingController ()->stylusTool ();

        switch (currentTool)
        {
        case UBStylusTool::Selector :
        case UBStylusTool::Play :
        {
            if (bIsDesktop) {
                event->ignore();
                return;
            }

            // Calling handleItemMousePress on a text item ends in the item being deselected, so the context menu becomes inoprent.
            // Could not find why in handleItemMousePress, so we simply make an exception for text items here
            UBGraphicsTextItem* textItem = dynamic_cast<UBGraphicsTextItem*>(getMovingItem());
            if (!textItem)
            {
                handleItemMousePress(event);
                event->accept();
            }
            break;
        }
        default:
            break;
        }
    }
}


void UBBoardView::mouseMoveEvent (QMouseEvent *event)
{
    //    static QTime lastCallTime;
    //    if (!lastCallTime.isNull()) {
    //        qDebug() << "time interval is " << lastCallTime.msecsTo(QTime::currentTime());
    //    }

    //  QTime mouseMoveTime = QTime::currentTime();
    if(!mIsDragInProgress && ((mapToScene(event->pos()) - mLastPressedMousePos).manhattanLength() < QApplication::startDragDistance())) {
        return;
    }

    mIsDragInProgress = true;
    mWidgetMoved = true;
    mLongPressTimer.stop();

    if (isAbsurdPoint (event->pos ())) {
        event->accept ();
        return;
    }

    if ((UBDrawingController::drawingController()->isDrawingTool()) && !mMouseButtonIsPressed)
        QGraphicsView::mouseMoveEvent(event);

    int currentTool = static_cast<int>(UBDrawingController::drawingController()->stylusTool());
    switch (currentTool) {

    case UBStylusTool::Hand : {
        if (!mMouseButtonIsPressed && !mTabletStylusIsPressed) {
            break;
        }
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        QPointF eventPosition = event->position();
#else
        QPointF eventPosition = event->localPos();
#endif
        qreal dx = eventPosition.x () - mPreviousPoint.x ();
        qreal dy = eventPosition.y () - mPreviousPoint.y ();
        mController->handScroll (dx, dy);
        mPreviousPoint = eventPosition;
        event->accept ();
    } break;

    case UBStylusTool::Selector :
    case UBStylusTool::Play : {
        if (bIsDesktop) {
            event->ignore();
            return;
        }

        bool rubberMove = (currentTool != (UBStylusTool::Play))
                && (mMouseButtonIsPressed || mTabletStylusIsPressed)
                && !getMovingItem();

        if (rubberMove) {
            QRect bandRect(mMouseDownPos, event->pos());

            bandRect = bandRect.normalized();

            if (!mUBRubberBand) {
                mUBRubberBand = new UBRubberBand(QRubberBand::Rectangle, this);
            }
            mUBRubberBand->setGeometry(bandRect);
            mUBRubberBand->show();

            //          QTime startTime = QTime::currentTime();
            //          QTime testTime = QTime::currentTime();
            QList<QGraphicsItem *> rubberItems = items(bandRect);
            //          qDebug() << "==================";
            //          qDebug() << "| ====rubber items" << testTime.msecsTo(QTime::currentTime());
            //          testTime = QTime::currentTime();
            foreach (QGraphicsItem *item, mJustSelectedItems) {
                if (!rubberItems.contains(item)) {
                    item->setSelected(false);
                    mJustSelectedItems.remove(item);
                }
            }
            //          qDebug() << "| ===foreach length" << testTime.msecsTo(QTime::currentTime());
            //          testTime = QTime::currentTime();

            int counter = 0;
            if (currentTool == UBStylusTool::Selector) {
                foreach (QGraphicsItem *item, items(bandRect)) {

                    if(item->type() == UBGraphicsItemType::PolygonItemType && item->parentItem())
                        item = item->parentItem();

                    if (item->type() == UBGraphicsW3CWidgetItem::Type
                            || item->type() == UBGraphicsPixmapItem::Type
                            || item->type() == UBGraphicsVideoItem::Type
                            || item->type() == UBGraphicsAudioItem::Type
                            || item->type() == UBGraphicsSvgItem::Type
                            || item->type() == UBGraphicsTextItem::Type
                            || item->type() == UBGraphicsStrokesGroup::Type
                            || item->type() == UBGraphicsGroupContainerItem::Type) {


                        if (!mJustSelectedItems.contains(item)) {
                            counter++;
                            item->setSelected(true);
                            mJustSelectedItems.insert(item);
                        }
                    }
                }
            }

            //          qDebug() << "| ==selected items count" << counter << '\n'
            //                   << "| ==selection time" << testTime.msecsTo(QTime::currentTime()) << '\n'
            //                   << "| =elapsed time " << startTime.msecsTo(QTime::currentTime()) << '\n'
            //                   << "==================";
            //          QCoreApplication::removePostedEvents(scene(), 0);
        }
        handleItemMouseMove(event);
    } break;

    case UBStylusTool::Text :
    case UBStylusTool::Capture : {
        if (mRubberBand && (mIsCreatingTextZone || mIsCreatingSceneGrabZone)) {
            mRubberBand->setGeometry(QRect(mMouseDownPos, event->pos()).normalized());
            event->accept();
        }
        else
            QGraphicsView::mouseMoveEvent (event);

    } break;

    default:
        if (!mTabletStylusIsPressed && scene()) {
            scene()->inputDeviceMove(mapToScene(UBGeometryUtils::pointConstrainedInRect(event->pos(), rect())) , mMouseButtonIsPressed, event->modifiers());
        }
        event->accept ();
    }

    //  qDebug() << "mouse move time" << mouseMoveTime.msecsTo(QTime::currentTime());
    //  lastCallTime = QTime::currentTime();

}

void UBBoardView::movingItemDestroyed(QObject*)
{
    setMovingItem(nullptr);
}

void UBBoardView::mouseReleaseEvent (QMouseEvent *event)
{
    UBStylusTool::Enum currentTool = (UBStylusTool::Enum)UBDrawingController::drawingController ()->stylusTool ();

    setToolCursor (currentTool);
    // first/ propagate device release to the scene
    if (scene())
        scene()->inputDeviceRelease(currentTool, event->modifiers());

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QPointF eventPosition = event->position();
#else
    QPointF eventPosition = event->localPos();
#endif

    if (currentTool == UBStylusTool::Selector)
    {
        if (bIsDesktop) {
            event->ignore();
            return;
        }

        UBGraphicsItem *graphicsItem = dynamic_cast<UBGraphicsItem*>(getMovingItem());
        if (graphicsItem)
            graphicsItem->Delegate()->commitUndoStep();

        bool bReleaseIsNeed = true;
        if (getMovingItem() != determineItemToPress(scene()->itemAt(this->mapToScene(eventPosition.toPoint()), QTransform())))
        {
            setMovingItem(nullptr);
            bReleaseIsNeed = false;
        }
        if (mWidgetMoved)
        {
            auto item = getMovingItem();

            if (item && item->type() == UBGraphicsWidgetItem::Type)
            {
                UBGraphicsWidgetItem* widgetItem = qgraphicsitem_cast<UBGraphicsWidgetItem *>(item);
                widgetItem->updatePosition();
            }

            mWidgetMoved = false;
            setMovingItem(nullptr);
        }
        else
        {
            if (getMovingItem())
            {
                if (!isCppTool(getMovingItem()) || UBGraphicsCurtainItem::Type == getMovingItem()->type())
                {
                    if (suspendedMousePressEvent)
                    {
                        QGraphicsView::mousePressEvent(suspendedMousePressEvent);     // suspendedMousePressEvent is deleted by old Qt event loop
                        setMovingItem(NULL);
                        delete suspendedMousePressEvent;
                        suspendedMousePressEvent = NULL;
                        bReleaseIsNeed = true;
                    }
                    else
                    {
                        if (isUBItem(getMovingItem()) &&
                                DelegateButton::Type != getMovingItem()->type() &&
                                UBGraphicsDelegateFrame::Type !=  getMovingItem()->type() &&
                                UBGraphicsCache::Type != getMovingItem()->type() &&
                                !(!isMultipleSelectionEnabled() && getMovingItem()->parentItem() && UBGraphicsWidgetItem::Type == getMovingItem()->type() && UBGraphicsGroupContainerItem::Type == getMovingItem()->parentItem()->type()))
                        {
                            bReleaseIsNeed = false;
                            if (getMovingItem()->isSelected() && isMultipleSelectionEnabled())
                                getMovingItem()->setSelected(false);
                            else
                                if (getMovingItem()->parentItem() && getMovingItem()->parentItem()->isSelected() && isMultipleSelectionEnabled())
                                    getMovingItem()->parentItem()->setSelected(false);
                                else
                                {
                                    if (getMovingItem()->isSelected())
                                        bReleaseIsNeed = true;

                                    UBGraphicsTextItem* textItem = dynamic_cast<UBGraphicsTextItem*>(getMovingItem());
                                    UBGraphicsMediaItem* movieItem = dynamic_cast<UBGraphicsMediaItem*>(getMovingItem());
                                    if(textItem)
                                        textItem->setSelected(true);
                                    else if(movieItem)
                                        movieItem->setSelected(true);
                                    else
                                        getMovingItem()->setSelected(true);
                                }

                        }
                    }
                }
                else
                    bReleaseIsNeed = true;
            }
            else
                bReleaseIsNeed = true;
        }

        if (bReleaseIsNeed)
        {
            QGraphicsView::mouseReleaseEvent (event);
        }
    }
    else if (currentTool == UBStylusTool::Text)
    {
        bool bReleaseIsNeed = true;
        if (getMovingItem() != determineItemToPress(scene()->itemAt(this->mapToScene(eventPosition.toPoint()), QTransform())))
        {
            setMovingItem(NULL);
            bReleaseIsNeed = false;
        }

        UBGraphicsItem *graphicsItem = dynamic_cast<UBGraphicsItem*>(getMovingItem());
        if (graphicsItem)
            graphicsItem->Delegate()->commitUndoStep();

        if (mWidgetMoved)
        {
            mWidgetMoved = false;
            setMovingItem(NULL);
            if (scene () && mRubberBand && mIsCreatingTextZone) {
                QRect rubberRect = mRubberBand->geometry ();

                UBGraphicsTextItem* textItem = scene()->addTextHtml ("", mapToScene (rubberRect.topLeft ()));
                event->accept ();

                UBDrawingController::drawingController ()->setStylusTool (UBStylusTool::Selector);

                textItem->setTextInteractionFlags(Qt::TextEditorInteraction);
                textItem->setSelected(true);

                UBGraphicsTextItemDelegate * textItemDelegate = dynamic_cast<UBGraphicsTextItemDelegate*>(textItem->Delegate());

                if (textItemDelegate)
                {
                    if (rubberRect.width() == 0)
                    {
                        textItem->setTextWidth(scene()->nominalSize().width() / mController->currentZoom() / 4.);
                    }
                    else if (rubberRect.width() <= (textItemDelegate->titleBarWidth() * mController->currentZoom()))
                    {
                        textItem->setTextWidth(textItemDelegate->titleBarWidth());
                    }
                    else // rubberRect.width() > (textItemDelegate->titleBarWidth() * mController->currentZoom())
                    {
                        textItem->setTextWidth(mapToScene(rubberRect).boundingRect().width());
                    }
                }
                else
                {
                    textItem->setTextWidth(scene()->nominalSize().width() / mController->currentZoom() / 4.);
                }
                textItem->setFocus();
            }
        }
        else if (getMovingItem() && (!isCppTool(getMovingItem()) || UBGraphicsCurtainItem::Type == getMovingItem()->type()))
        {
            if (suspendedMousePressEvent)
            {
                QGraphicsView::mousePressEvent(suspendedMousePressEvent);     // suspendedMousePressEvent is deleted by old Qt event loop
                setMovingItem(NULL);
                delete suspendedMousePressEvent;
                suspendedMousePressEvent = NULL;
                bReleaseIsNeed = true;
            }
            else{
                if (isUBItem(getMovingItem()) &&
                        DelegateButton::Type != getMovingItem()->type() &&
                        QGraphicsSvgItem::Type !=  getMovingItem()->type() &&
                        UBGraphicsDelegateFrame::Type !=  getMovingItem()->type() &&
                        UBGraphicsCache::Type != getMovingItem()->type() &&
                        !(!isMultipleSelectionEnabled() && getMovingItem()->parentItem() && UBGraphicsWidgetItem::Type == getMovingItem()->type() && UBGraphicsGroupContainerItem::Type == getMovingItem()->parentItem()->type()))
                {
                    bReleaseIsNeed = false;
                    if (getMovingItem()->isSelected() && isMultipleSelectionEnabled())
                        getMovingItem()->setSelected(false);
                    else
                        if (getMovingItem()->parentItem() && getMovingItem()->parentItem()->isSelected() && isMultipleSelectionEnabled())
                            getMovingItem()->parentItem()->setSelected(false);
                        else
                        {
                            if (getMovingItem()->isSelected())
                                bReleaseIsNeed = true;

                            getMovingItem()->setSelected(true);
                        }

                }
            }
        }
        else
            bReleaseIsNeed = true;

        if (bReleaseIsNeed)
        {
            QGraphicsView::mouseReleaseEvent (event);
        }
    }
    else if (currentTool == UBStylusTool::Play) {
        if (bIsDesktop) {
            event->ignore();
            return;
        }

        if (mWidgetMoved)
        {
            if (getMovingItem())
            {
                getMovingItem()->setSelected(false);
                setMovingItem(NULL);
            }
            mWidgetMoved = false;
        }
        else {
            if (suspendedMousePressEvent) {
                QGraphicsView::mousePressEvent(suspendedMousePressEvent);     // suspendedMousePressEvent is deleted by old Qt event loop
                setMovingItem(NULL);
                delete suspendedMousePressEvent;
                suspendedMousePressEvent = NULL;
            }
        }
        QGraphicsView::mouseReleaseEvent (event);
    }
    else if (currentTool == UBStylusTool::Capture)
    {

        if (scene () && mRubberBand && mIsCreatingSceneGrabZone && mRubberBand->geometry ().width () > 16)
        {
            QRect rect = mRubberBand->geometry ();
            QPointF sceneTopLeft = mapToScene (rect.topLeft ());
            QPointF sceneBottomRight = mapToScene (rect.bottomRight ());
            QRectF sceneRect (sceneTopLeft, sceneBottomRight);

            mController->grabScene (sceneRect);

            event->accept ();
        }
        else
        {
            QGraphicsView::mouseReleaseEvent (event);
        }

        mIsCreatingSceneGrabZone = false;
    }
    else
    {
        if (mPendingStylusReleaseEvent || mMouseButtonIsPressed)
        {
            event->accept ();
        }
    }


    if (mUBRubberBand) {
        mUBRubberBand->hide();
        delete mUBRubberBand;
        mUBRubberBand = NULL;
    }

    if (mRubberBand) {
        mRubberBand->hide();
        delete mRubberBand;
        mRubberBand = NULL;
    }

    mMouseButtonIsPressed = false;
    mPendingStylusReleaseEvent = false;
    mTabletStylusIsPressed = false;
    setMovingItem(NULL);

    mLongPressTimer.stop();
    scene()->updateSelectionFrame();
}

void UBBoardView::forcedTabletRelease ()
{
    if (mMouseButtonIsPressed || mTabletStylusIsPressed || mPendingStylusReleaseEvent)
    {
        qWarning () << "dirty mouse/tablet state:";
        qWarning () << "mMouseButtonIsPressed =" << mMouseButtonIsPressed;
        qWarning () << "mTabletStylusIsPressed = " << mTabletStylusIsPressed;
        qWarning () << "mPendingStylusReleaseEvent" << mPendingStylusReleaseEvent;
        qWarning () << "forcing device release";

        scene ()->inputDeviceRelease ();

        mMouseButtonIsPressed = false;
        mTabletStylusIsPressed = false;
        mPendingStylusReleaseEvent = false;
    }
}

void UBBoardView::mouseDoubleClickEvent (QMouseEvent *event)
{
    // We don't want a double click, we want two clicks
    mousePressEvent (event);
}

void UBBoardView::wheelEvent (QWheelEvent *wheelEvent)
{
    if (!isInteractive())
    {
        // ignore event on non-interactive views
        wheelEvent->accept();
        return;
    }

    // Zoom in/out when Ctrl is pressed
    if (wheelEvent->modifiers() == Qt::ControlModifier && wheelEvent->angleDelta().x() == 0)
    {
        qreal angle = wheelEvent->angleDelta().y();
        qreal zoomBase = UBSettings::settings()->boardZoomBase->get().toDouble();
        qreal zoomFactor = qPow(zoomBase, angle);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
        mController->zoom(zoomFactor, mapToScene(wheelEvent->position().toPoint()));
#else
        mController->zoom(zoomFactor, mapToScene(wheelEvent->pos()));
#endif
        wheelEvent->accept();
        return;
    }

    QList<QGraphicsItem *> selItemsList = scene()->selectedItems();
    // if items selected, then forward mouse wheel event to item
    if( selItemsList.count() > 0 )
    {
        // only one selected item possible, so we will work with first item only
        QGraphicsItem * selItem = selItemsList[0];

        // get items list under mouse cursor
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
        QPointF scenePos = mapToScene(wheelEvent->position().toPoint());
#else
        QPointF scenePos = mapToScene(wheelEvent->pos());
#endif
        QList<QGraphicsItem *> itemsList = scene()->items(scenePos);

        bool isSelectedAndMouseHower = itemsList.contains(selItem);
        if(isSelectedAndMouseHower)
        {
            QTransform previousTransform = viewportTransform();
            QGraphicsView::wheelEvent(wheelEvent);

            if (previousTransform != viewportTransform())
            {
                // processing the event changed the transformation
                UBApplication::applicationController->adjustDisplayView();
            }

            return;
        }
    }

    // event not handled, send it to QAbstractScrollArea to scroll with wheel event
    QAbstractScrollArea::wheelEvent(wheelEvent);
    UBApplication::applicationController->adjustDisplayView();
}

void UBBoardView::leaveEvent (QEvent * event)
{
    if (scene ())
        scene ()->leaveEvent (event);

    mJustSelectedItems.clear();

    QGraphicsView::leaveEvent (event);
}

void UBBoardView::drawItems (QPainter *painter, int numItems, QGraphicsItem* items[], const QStyleOptionGraphicsItem options[])
{
    if (!mFilterZIndex)
        QGraphicsView::drawItems (painter, numItems, items, options);
    else
    {
        int count = 0;

        QGraphicsItem** itemsFiltered = new QGraphicsItem*[numItems];
        QStyleOptionGraphicsItem *optionsFiltered = new QStyleOptionGraphicsItem[numItems];

        for (int i = 0; i < numItems; i++)
        {
            if (shouldDisplayItem (items[i]))
            {
                itemsFiltered[count] = items[i];
                optionsFiltered[count] = options[i];
                count++;
            }
        }

        QGraphicsView::drawItems (painter, count, itemsFiltered, optionsFiltered);

        delete[] optionsFiltered;
        delete[] itemsFiltered;
    }
}

void UBBoardView::dragMoveEvent(QDragMoveEvent *event)
{
    QGraphicsView::dragMoveEvent(event);
    event->acceptProposedAction();
}

void UBBoardView::dropEvent (QDropEvent *event)
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QPointF eventPosition = event->position();
#else
    QPointF eventPosition = event->pos();
#endif
    QGraphicsItem *onItem = itemAt(eventPosition.x(),eventPosition.y());
    if (onItem && onItem->type() == UBGraphicsWidgetItem::Type && onItem->acceptDrops())
    {
        //items like images, sounds, etc.. can be passed to the board or to an application or interactivity. Both actions are acceptable.
        // We should ask the user what he wanted to achieve when object is dropped over a widget.
        if (UBApplication::mainWindow->yesNoQuestion(tr("Is it for Board or Widget ?"),
                                                     tr("Are you trying to drop the object(s) inside the widget ?")))
        {
            QGraphicsView::dropEvent(event);
        }
        else
        {
            if (!event->source()
                        || qobject_cast<UBDocumentThumbnailsView *>(event->source())
                        || qobject_cast<QWebEngineView*>(event->source())
                        || qobject_cast<QListView *>(event->source()))
            {
                    mController->processMimeData (event->mimeData(), mapToScene (eventPosition.toPoint()));
                    event->acceptProposedAction();
            }
        }
    }
    else
    {
        if (!event->source()
                    || qobject_cast<UBDocumentThumbnailsView *>(event->source())
                    || qobject_cast<QWebEngineView*>(event->source())
                    || qobject_cast<QListView *>(event->source()))
        {
                mController->processMimeData (event->mimeData(), mapToScene (eventPosition.toPoint()));
                event->acceptProposedAction();
        }
    }

    //prevent features in UBFeaturesWidget deletion from the model when event is processing inside
    //Qt base classes
    if (event->dropAction() == Qt::MoveAction) {
        event->setDropAction(Qt::CopyAction);
    }

    mMouseButtonIsPressed = false;
    mPendingStylusReleaseEvent = false;
    mTabletStylusIsPressed = false;
    setMovingItem(nullptr);
}

void UBBoardView::resizeEvent (QResizeEvent * event)
{
    const qreal maxWidth = width () * 10;
    const qreal maxHeight = height () * 10;

    setSceneRect (-(maxWidth / 2), -(maxHeight / 2), maxWidth, maxHeight);
    centerOn (0, 0);

    emit resized (event);
}

void UBBoardView::paintEvent(QPaintEvent *event)
{
    QGraphicsView::paintEvent(event);

    // ignore paint events under the left palette
    int paletteWidth = UBApplication::boardController->paletteManager()->leftPalette()->width();

    if (event->rect().right() >= paletteWidth)
    {
        emit painted(mapToScene(event->rect()).boundingRect());
    }
}

void UBBoardView::drawBackground (QPainter *painter, const QRectF &rect)
{
    // draw the background of the QGraphicsScene
    QGraphicsView::drawBackground(painter, rect);

    if (testAttribute (Qt::WA_TranslucentBackground))
    {
        return;
    }

    if (!mFilterZIndex && scene ())
    {
        QSize pageNominalSize = scene ()->nominalSize ();

        if (pageNominalSize.isValid ())
        {
            qreal penWidth = 8.0 / transform ().m11 ();

            QRectF pageRect (pageNominalSize.width () / -2, pageNominalSize.height () / -2
                             , pageNominalSize.width (), pageNominalSize.height ());

            pageRect.adjust (-penWidth / 2, -penWidth / 2, penWidth / 2, penWidth / 2);

            QColor docSizeColor;

            if (scene ()->isDarkBackground ())
                docSizeColor = UBSettings::documentSizeMarkColorDarkBackground;
            else
                docSizeColor = UBSettings::documentSizeMarkColorLightBackground;

            QPen pen (docSizeColor);
            pen.setWidth (penWidth);
            painter->setPen (pen);
            painter->drawRect (pageRect);
        }
    }
}

void UBBoardView::drawForeground(QPainter* painter, const QRectF& rect)
{
    QTransform transform{viewportTransform()};
    QRect viewportRect(0, 0, viewport()->width(), viewport()->height());
    QRectF visible{mapToScene(viewportRect).boundingRect()};

    painter->save();
    QColor color{0x808080};
    color.setAlphaF(0.3);
    QBrush brush{color};
    painter->setBrush(brush);
    painter->setPen(Qt::NoPen);

    if (mMargins.left())
    {
        QRectF cover{visible};
        auto leftMargin = mMargins.left() / transform.m11();
        cover.setRight(cover.left() + leftMargin);
        painter->drawRect(cover);
    }

    if (mMargins.right())
    {
        QRectF cover{visible};
        auto rightMargin = mMargins.right() / transform.m11();
        cover.setLeft(cover.right() - rightMargin);
        painter->drawRect(cover);
    }

    if (mMargins.top())
    {
        QRectF cover{visible};
        auto topMargin = mMargins.top() / transform.m22();
        cover.setBottom(cover.top() + topMargin);
        painter->drawRect(cover);
    }

    if (mMargins.bottom())
    {
        QRectF cover{visible};
        auto bottomMargin = mMargins.bottom() / transform.m22();
        cover.setTop(cover.bottom() - bottomMargin);
        painter->drawRect(cover);
    }

    painter->restore();
}

void UBBoardView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    if (scene())
        scene()->controlViewportChanged();
}

void UBBoardView::settingChanged (QVariant newValue)
{
    Q_UNUSED (newValue);

    mPenPressureSensitive = UBSettings::settings ()->boardPenPressureSensitive->get ().toBool ();
    mMarkerPressureSensitive = UBSettings::settings ()->boardMarkerPressureSensitive->get ().toBool ();
    mUseHighResTabletEvent = UBSettings::settings ()->boardUseHighResTabletEvent->get ().toBool ();
}

void UBBoardView::virtualKeyboardActivated(bool b)
{
    UBPlatformUtils::setWindowNonActivableFlag(this, b);
    mVirtualKeyboardActive = b;
    setInteractive(!b);
}


// Apple remote desktop sends funny events when the transmission is bad

bool UBBoardView::isAbsurdPoint(QPoint point)
{
    return QGuiApplication::screenAt(mapToGlobal(point)) == nullptr;
}

void UBBoardView::focusOutEvent (QFocusEvent * event)
{
    Q_UNUSED (event);
}

void UBBoardView::setToolCursor (int tool)
{
    QWidget *controlViewport = viewport ();
    switch (tool)
    {
    case UBStylusTool::Pen:
        controlViewport->setCursor (UBResources::resources ()->penCursor);
        break;
    case UBStylusTool::Eraser:
        controlViewport->setCursor (UBResources::resources ()->eraserCursor);
        break;
    case UBStylusTool::Marker:
        controlViewport->setCursor (UBResources::resources ()->markerCursor);
        break;
    case UBStylusTool::Pointer:
        controlViewport->setCursor (UBResources::resources ()->pointerCursor);
        break;
    case UBStylusTool::Hand:
        controlViewport->setCursor (UBResources::resources ()->handCursor);
        break;
    case UBStylusTool::ZoomIn:
        controlViewport->setCursor (UBResources::resources ()->zoomInCursor);
        break;
    case UBStylusTool::ZoomOut:
        controlViewport->setCursor (UBResources::resources ()->zoomOutCursor);
        break;
    case UBStylusTool::Selector:
        controlViewport->setCursor (UBResources::resources ()->arrowCursor);
        break;
    case UBStylusTool::Play:
        controlViewport->setCursor (UBResources::resources ()->playCursor);
        break;
    case UBStylusTool::Line:
        controlViewport->setCursor (UBResources::resources ()->penCursor);
        break;
    case UBStylusTool::Text:
        controlViewport->setCursor (UBResources::resources ()->textCursor);
        break;
    case UBStylusTool::Capture:
        controlViewport->setCursor (UBResources::resources ()->penCursor);
        break;
    default:
        Q_ASSERT (false);
        //failsafe
        controlViewport->setCursor (UBResources::resources ()->penCursor);
    }
}


bool UBBoardView::hasSelectedParents(QGraphicsItem * item)
{
    if (item->isSelected())
        return true;
    if (item->parentItem()==NULL)
        return false;
    return hasSelectedParents(item->parentItem());
}
