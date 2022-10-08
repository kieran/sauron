#ifdef DEBUG
#define log(s) (Serial.print(s))
#define logf(s,v1) (Serial.printf(s,v1))
#define logf2(s,v1,v2) (Serial.printf(s,v1,v2))
#else
#define log(s)
#define logf(s,v1)
#define logf2(s,v1,v2)
#endif
