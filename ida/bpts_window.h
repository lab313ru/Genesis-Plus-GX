#pragma once

#include <QtWidgets>

class BptsWindow : public QObject
{
    Q_OBJECT

public:
    BptsWindow(QObject* _parent) : QObject(_parent) {}

private slots:
    void addBreakpoint();
    void delBreakpoint();
    void clrBreakpoints();
    void rowDoubleClick(int row, int col);
};