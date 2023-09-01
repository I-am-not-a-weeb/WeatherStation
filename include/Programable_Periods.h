#pragma once

#include <functional>

struct TimeOfDay
{
    unsigned char hour;
    unsigned char minute;
    unsigned char second;
};


class ProgramablePeriods
{
private:
    std::vector<SinglePointPeriod> single_point_periods;
    std::vector<DoublePointPeriod> double_point_periods;
public:
    ProgramablePeriods();
    {

    }
    ProgrammablePeriods(unsigned int size_s,unsigned int size_d)
    {
        single_point_periods.reserve(size_s);
        double_point_periods.reserve(size_d);
    }
    unsigned int size()
    {
        return single_point_periods.size() + double_point_periods.size();
    }

};

class SinglePointPeriod
{
protected:
    TimeOfDay first_point;
public:
    SinglePointPeriod(unsigned char hour);
    SinglePointPeriod(unsigned char hour, unsigned char minute);
    SinglePointPeriod(unsigned char hour, unsigned char minute, unsigned char second);
    
};

class DoublePointPeriod : public SinglePointPeriod
{
private:
    TimeOfDay second_point;
public:
    
};
