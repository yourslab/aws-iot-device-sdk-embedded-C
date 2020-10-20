#ifndef TRANSPORT_CONFIG_H_
#define TRANSPORT_CONFIG_H_

/* Forward declaration must be used for these types. */
struct OpensslParams;
typedef struct OpensslParams     OpensslParams_t;

struct PlaintextParams;
typedef struct PlaintextParams   PlaintextParams_t;

struct NetworkContext
{
    OpensslParams_t * pOpensslParams;
    PlaintextParams_t * pPlaintextParams;
};

#endif /* ifndef TRANSPORT_CONFIG_H_ */
