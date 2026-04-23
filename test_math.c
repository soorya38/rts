#include <stdio.h>
#include <math.h>

int main() {
    double osm_bbox_west = 76.9328;
    double osm_bbox_east = 76.9928;
    double osm_bbox_north = 11.0218;
    double osm_bbox_south = 10.9818;
    int z = 14;

    double tx0_exact = (osm_bbox_west + 180.0) / 360.0 * (1 << z);
    double tx1_exact = (osm_bbox_east + 180.0) / 360.0 * (1 << z);
    double r_north = osm_bbox_north * 3.14159265358979323846 / 180.0;
    double ty0_exact = (1.0 - log(tan(r_north) + 1.0/cos(r_north)) / 3.14159265358979323846) / 2.0 * (1 << z);
    double r_south = osm_bbox_south * 3.14159265358979323846 / 180.0;
    double ty1_exact = (1.0 - log(tan(r_south) + 1.0/cos(r_south)) / 3.14159265358979323846) / 2.0 * (1 << z);

    double exact_cols = tx1_exact - tx0_exact;
    double exact_rows = ty1_exact - ty0_exact;
    
    printf("exact_cols: %f, exact_rows: %f\n", exact_cols, exact_rows);

    return 0;
}
