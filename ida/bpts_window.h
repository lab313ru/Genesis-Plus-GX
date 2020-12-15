#pragma once

#include <QtWidgets>

class BptsWindow : public QWidget
{
    Q_OBJECT

public:
    BptsWindow(QWidget* _parent) : QWidget(_parent) {}

private slots:
    void addBreakpoint();
    void delBreakpoint();
    void clrBreakpoints();
    void rowDoubleClick(int row, int col);
};