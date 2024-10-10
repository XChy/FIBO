#pragma once

class Tracer {
public:
    void log();
    static Tracer* get();
private:
};
