#ifndef TIMER_PP
#define TIMER_HPP


#include<chrono>


class Timer{
    using clock = std::chrono::high_resolution_clock;
    clock::time_point startTime;




public:

    void start(){
        startTime = clock::now();
    }

    double stop(){
        auto endTime = clock::now();
        std::chrono::duration<double , std::micro> duration = endTime - startTime;
        return duration.count();
    }


};


#endif 