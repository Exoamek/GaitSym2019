/*
 *  DataTarget.h
 *  GaitSymODE
 *
 *  Created by Bill Sellers on Sat May 22 2004.
 *  Copyright (c) 2004 Bill Sellers. All rights reserved.
 *
 */

#ifndef DataTarget_h
#define DataTarget_h

#include "NamedObject.h"
#include "SmartEnum.h"

#include <cfloat>
#include <tuple>

class Body;
class SimulationWindow;

class DataTarget: public NamedObject
{
public:
    DataTarget();

    SMART_ENUM(MatchType, matchTypeStrings, matchTypeCount, Linear, Square, Raw);
    SMART_ENUM(InterpolationType, interpolationTypeStrings, interpolationTypeCount, Punctuated, Continuous);

//    void SetTargetTimes(int size, double *targetTimes);
//    virtual void SetTargetValues(int size, double *values) = 0;

//    int TargetMatch(double time, double tolerance);
//    int GetLastMatchIndex();

    void setIntercept(double intercept);
    void setSlope(double slope);
    void setMatchType(MatchType t);
    void setAbortThreshold(double a);

    std::tuple<double, bool> calculateMatchValue(double time);

    double positiveFunction(double v);


//    int TargetTimeListLength() const;

//    const double *TargetTimeList() const;

    virtual std::string dumpToString();
    virtual std::string *createFromAttributes();
    virtual void saveToAttributes();
    virtual void appendToAttributes();

    virtual double calculateError(double time) = 0;
    virtual double calculateError(size_t index) = 0;

protected:
    std::vector<double> *targetTimeList();

private:
    double m_intercept = 0;
    double m_slope = 0;
    MatchType m_matchType = MatchType::Linear;
    InterpolationType m_interpolationType = InterpolationType::Punctuated;
    double m_abortBelow = -DBL_MAX;
    double m_abortAbove = DBL_MAX;
    std::vector<double> m_targetTimeList;
    size_t m_lastIndex = SIZE_MAX;
    double m_lastValue = 0;
};

#endif

