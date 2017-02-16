#ifndef PISTON_H
#define PISTON_H

#include "gpo.h"

class Piston : public Gpo
{
public:
    Piston(uint32_t mask);

    void push();
    void pull();
};

#endif // PISTON_H
