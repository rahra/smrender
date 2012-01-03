#ifndef SMRULES_H
#define SMRULES_H

//void mk_chart_coords(int, int, struct rdata*, double*, double*);
//void mk_paper_coords(double, double, struct rdata*, int*, int*);

int act_image(struct onode*, struct rdata*, struct onode*);
int act_caption(struct onode*, struct rdata*, struct onode*);
int act_open_poly(struct onode*, struct rdata*, struct onode*);
int act_fill_poly(struct onode*, struct rdata*, struct onode*);

#endif

