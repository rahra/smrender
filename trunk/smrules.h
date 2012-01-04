#ifndef SMRULES_H
#define SMRULES_H


int act_image(struct onode*, struct rdata*, struct orule*);
int act_caption(struct onode*, struct rdata*, struct orule*);
int act_open_poly(struct onode*, struct rdata*, struct orule*);
int act_fill_poly(struct onode*, struct rdata*, struct orule*);


#endif

