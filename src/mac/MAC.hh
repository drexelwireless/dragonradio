#ifndef MAC_H_
#define MAC_H_

/** @brief A MAC protocol. */
class MAC
{
public:
    MAC() = default;
    virtual ~MAC() = default;

    /** @brief Stop processing packets. */
    virtual void stop(void) = 0;
};

#endif /* MAC_H_ */
