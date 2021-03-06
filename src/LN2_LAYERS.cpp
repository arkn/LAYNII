
// TODO(Faruk): Curvature shows some artifacts in rim_circles test case. Needs
// further investiation.
// TODO(Faruk): First implementation of equi-volume works but it needs
// extensive testing. I might need to smooth previous derivatives
// (hotspots, curvature etc.) a bit to make layering smoother on empirical
// data overall.
// TODO(Faruk): Memory usage is a bit sloppy for now. Low priority but might
// need to have a look at it in the future if we start hitting ram limits.
// TODO(Faruk): Put neighbour visits into a function.

#include "../dep/laynii_lib.h"

int show_help(void) {
    printf(
    "LN2_LAYERS: Generates equi-distant cortical gray matter layers with\n"
    "            an option to generate equi-volume layers in addition.\n"
    "\n"
    "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
    "!! BEWARE! WORK IN PROGRESS... USE WITH CAUTION !!\n"
    "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
    "\n"
    "Usage:\n"
    "    LN2_LAYERS -rim rim.nii\n"
    "    LN2_LAYERS -rim rim.nii -nr_layers 10\n"
    "    LN2_LAYERS -rim rim.nii -nr_layers 10 -equivol\n"
    "    LN2_LAYERS -rim rim.nii -nr_layers 10 -equivol -iter_smooth 1000\n"
    "\n"
    "Options:\n"
    "    -help         : Show this help.\n"
    "    -rim          : Specify input dataset.\n"
    "    -nr_layers    : Number of layers. Default is 3.\n"
    "    -equivol      : (Optional) Create equi-volume layers.\n"
    "    -iter_smooth  : (Optional) Number of smoothing iterations. Default\n"
    "                    is 100. Only used together with '-equivol' flag. Use\n"
    "                    larger values when equi-volume layers are jagged.\n"
    "    -debug        : (Optional) Save extra intermediate outputs.\n"
    "\n");
    return 0;
}

int main(int argc, char*  argv[]) {
    nifti_image *nii1 = NULL;
    char* fin = NULL;
    uint16_t ac, nr_layers = 3;
    uint16_t iter_smooth = 100;
    bool mode_equivol = false, mode_debug = false;

    // Process user options
    if (argc < 2) return show_help();
    for (ac = 1; ac < argc; ac++) {
        if (!strncmp(argv[ac], "-h", 2)) {
            return show_help();
        } else if (!strcmp(argv[ac], "-rim")) {
            if (++ac >= argc) {
                fprintf(stderr, "** missing argument for -rim\n");
                return 1;
            }
            fin = argv[ac];
        } else if (!strcmp(argv[ac], "-nr_layers")) {
            if (++ac >= argc) {
                fprintf(stderr, "** missing argument for -nr_layers\n");
            } else {
                nr_layers = atof(argv[ac]);
            }
        } else if (!strcmp(argv[ac], "-iter_smooth")) {
            if (++ac >= argc) {
                fprintf(stderr, "** missing argument for -iter_smooth\n");
            } else {
                iter_smooth = atof(argv[ac]);
            }
        } else if (!strcmp(argv[ac], "-equivol")) {
            mode_equivol = true;
        } else if (!strcmp(argv[ac], "-debug")) {
            mode_debug = true;
        } else {
            fprintf(stderr, "** invalid option, '%s'\n", argv[ac]);
            return 1;
        }
    }

    if (!fin) {
        fprintf(stderr, "** missing option '-rim'\n");
        return 1;
    }

    // Read input dataset, including data
    nii1 = nifti_image_read(fin, 1);
    if (!nii1) {
        fprintf(stderr, "** failed to read NIfTI from '%s'\n", fin);
        return 2;
    }

    log_welcome("LN2_LAYERS");
    log_nifti_descriptives(nii1);

    cout << "\n  Nr. layers: " << nr_layers << endl;

    // Get dimensions of input
    const uint32_t size_x = nii1->nx;
    const uint32_t size_y = nii1->ny;
    const uint32_t size_z = nii1->nz;

    const uint32_t end_x = size_x - 1;
    const uint32_t end_y = size_y - 1;
    const uint32_t end_z = size_z - 1;

    const uint32_t nr_voxels = size_z * size_y * size_x;

    const float dX = nii1->pixdim[1];
    const float dY = nii1->pixdim[2];
    const float dZ = nii1->pixdim[3];

    // Short diagonals
    const float dia_xy = sqrt(dX * dX + dY * dY);
    const float dia_xz = sqrt(dX * dX + dZ * dZ);
    const float dia_yz = sqrt(dY * dY + dZ * dZ);
    // Long diagonals
    const float dia_xyz = sqrt(dX * dX + dY * dY + dZ * dZ);

    // ========================================================================
    // Fix input datatype issues
    nifti_image* nii_rim = copy_nifti_as_int32(nii1);
    int32_t* nii_rim_data = static_cast<int32_t*>(nii_rim->data);

    // Prepare required nifti images
    nifti_image* nii_layers  = copy_nifti_as_int32(nii_rim);
    int32_t* nii_layers_data = static_cast<int32_t*>(nii_layers->data);
    // Setting zero
    for (uint32_t i = 0; i != nr_voxels; ++i) {
        *(nii_layers_data + i) = 0;
    }

    nifti_image* innerGM_step = copy_nifti_as_float32(nii_layers);
    float* innerGM_step_data = static_cast<float*>(innerGM_step->data);
    nifti_image* innerGM_dist = copy_nifti_as_float32(nii_layers);
    float* innerGM_dist_data = static_cast<float*>(innerGM_dist->data);

    nifti_image* outerGM_step = copy_nifti_as_float32(nii_layers);
    float* outerGM_step_data = static_cast<float*>(outerGM_step->data);
    nifti_image* outerGM_dist = copy_nifti_as_float32(nii_layers);
    float* outerGM_dist_data = static_cast<float*>(outerGM_dist->data);

    nifti_image* err_dist = copy_nifti_as_float32(nii_layers);
    float* err_dist_data = static_cast<float*>(err_dist->data);

    nifti_image* innerGM_id = copy_nifti_as_int32(nii_layers);
    int32_t* innerGM_id_data = static_cast<int32_t*>(innerGM_id->data);
    nifti_image* outerGM_id = copy_nifti_as_int32(nii_layers);
    int32_t* outerGM_id_data = static_cast<int32_t*>(outerGM_id->data);

    nifti_image* innerGM_prevstep_id = copy_nifti_as_int32(nii_layers);
    int32_t* innerGM_prevstep_id_data =
        static_cast<int32_t*>(innerGM_prevstep_id->data);
    nifti_image* outerGM_prevstep_id = copy_nifti_as_int32(nii_layers);
    int32_t* outerGM_prevstep_id_data =
        static_cast<int32_t*>(outerGM_prevstep_id->data);
    nifti_image* normdistdiff = copy_nifti_as_float32(nii_layers);
    float* normdistdiff_data = static_cast<float*>(normdistdiff->data);

    nifti_image* nii_columns = copy_nifti_as_int32(nii_layers);
    int32_t* nii_columns_data = static_cast<int32_t*>(nii_columns->data);

    nifti_image* midGM = copy_nifti_as_int32(nii_layers);
    int32_t* midGM_data = static_cast<int32_t*>(midGM->data);
    nifti_image* midGM_id = copy_nifti_as_int32(nii_layers);
    int32_t* midGM_id_data = static_cast<int32_t*>(midGM_id->data);

    nifti_image* hotspots = copy_nifti_as_int32(nii_layers);
    int32_t* hotspots_data = static_cast<int32_t*>(hotspots->data);
    nifti_image* curvature = copy_nifti_as_float32(nii_layers);
    float* curvature_data = static_cast<float*>(curvature->data);
    nifti_image* thickness = copy_nifti_as_float32(nii_layers);
    float* thickness_data = static_cast<float*>(thickness->data);

    // ========================================================================
    // Grow from WM
    // ========================================================================
    cout << "\n  Start growing from inner GM (WM-facing border)..."   << endl;

    // Initialize grow volume
    for (uint32_t i = 0; i != nr_voxels; ++i) {
        if (*(nii_rim_data + i) == 2) {  // WM boundary voxels within GM
            *(innerGM_step_data + i) = 1.;
            *(innerGM_dist_data + i) = 0.;
            *(innerGM_id_data + i) = i;
        } else {
            *(innerGM_step_data + i) = 0.;
            *(innerGM_dist_data + i) = 0.;
        }
    }

    uint32_t grow_step = 1, voxel_counter = nr_voxels;
    uint32_t ix, iy, iz, j, k;
    float d;
    while (voxel_counter != 0) {
        voxel_counter = 0;
        for (uint32_t i = 0; i != nr_voxels; ++i) {
            if (*(innerGM_step_data + i) == grow_step) {
                tie(ix, iy, iz) = ind2sub_3D(i, size_x, size_y);
                voxel_counter += 1;

                // ------------------------------------------------------------
                // 1-jump neighbours
                // ------------------------------------------------------------
                if (ix > 0) {
                    j = sub2ind_3D(ix-1, iy, iz, size_x, size_y);
                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dX;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x) {
                    j = sub2ind_3D(ix+1, iy, iz, size_x, size_y);
                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dX;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iy > 0) {
                    j = sub2ind_3D(ix, iy-1, iz, size_x, size_y);
                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dY;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iy < end_y) {
                    j = sub2ind_3D(ix, iy+1, iz, size_x, size_y);
                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dY;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iz > 0) {
                    j = sub2ind_3D(ix, iy, iz-1, size_x, size_y);
                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dZ;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iz < end_z) {
                    j = sub2ind_3D(ix, iy, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dZ;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }

                // ------------------------------------------------------------
                // 2-jump neighbours
                // ------------------------------------------------------------
                if (ix > 0 && iy > 0) {
                    j = sub2ind_3D(ix-1, iy-1, iz, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xy;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix > 0 && iy < end_y) {
                    j = sub2ind_3D(ix-1, iy+1, iz, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xy;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iy > 0) {
                    j = sub2ind_3D(ix+1, iy-1, iz, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xy;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iy < end_y) {
                    j = sub2ind_3D(ix+1, iy+1, iz, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xy;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iy > 0 && iz > 0) {
                    j = sub2ind_3D(ix, iy-1, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_yz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iy > 0 && iz < end_z) {
                    j = sub2ind_3D(ix, iy-1, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_yz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iy < end_y && iz > 0) {
                    j = sub2ind_3D(ix, iy+1, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_yz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iy < end_y && iz < end_z) {
                    j = sub2ind_3D(ix, iy+1, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_yz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix > 0 && iz > 0) {
                    j = sub2ind_3D(ix-1, iy, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iz > 0) {
                    j = sub2ind_3D(ix+1, iy, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix > 0 && iz < end_z) {
                    j = sub2ind_3D(ix-1, iy, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iz < end_z) {
                    j = sub2ind_3D(ix+1, iy, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }

                // ------------------------------------------------------------
                // 3-jump neighbours
                // ------------------------------------------------------------
                if (ix > 0 && iy > 0 && iz > 0) {
                    j = sub2ind_3D(ix-1, iy-1, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xyz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix > 0 && iy > 0 && iz < end_z) {
                    j = sub2ind_3D(ix-1, iy-1, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xyz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix > 0 && iy < end_y && iz > 0) {
                    j = sub2ind_3D(ix-1, iy+1, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xyz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iy > 0 && iz > 0) {
                    j = sub2ind_3D(ix+1, iy-1, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xyz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix > 0 && iy < end_y && iz < end_z) {
                    j = sub2ind_3D(ix-1, iy+1, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xyz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iy > 0 && iz < end_z) {
                    j = sub2ind_3D(ix+1, iy-1, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xyz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iy < end_y && iz > 0) {
                    j = sub2ind_3D(ix+1, iy+1, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xyz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iy < end_y && iz < end_z) {
                    j = sub2ind_3D(ix+1, iy+1, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(innerGM_dist_data + i) + dia_xyz;
                        if (d < *(innerGM_dist_data + j)
                            || *(innerGM_dist_data + j) == 0) {
                            *(innerGM_dist_data + j) = d;
                            *(innerGM_step_data + j) = grow_step + 1;
                            *(innerGM_id_data + j) = *(innerGM_id_data + i);
                            *(innerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
            }
        }
        grow_step += 1;
    }
    if (mode_debug) {
        save_output_nifti(fin, "innerGM_step", innerGM_step, false);
        save_output_nifti(fin, "innerGM_dist", innerGM_dist, false);
        save_output_nifti(fin, "innerGM_id", innerGM_id, false);
    }

    // ========================================================================
    // Grow from CSF
    // ========================================================================
    cout << "\n  Start growing from outer GM..."   << endl;

    for (uint32_t i = 0; i != nr_voxels; ++i) {
        if (*(nii_rim_data + i) == 1) {
            *(outerGM_step_data + i) = 1.;
            *(outerGM_dist_data + i) = 0.;
            *(outerGM_id_data + i) = i;
        } else {
            *(outerGM_step_data + i) = 0.;
            *(outerGM_dist_data + i) = 0.;
        }
    }

    grow_step = 1, voxel_counter = nr_voxels;
    while (voxel_counter != 0) {
        voxel_counter = 0;
        for (uint32_t i = 0; i != nr_voxels; ++i) {
            if (*(outerGM_step_data + i) == grow_step) {
                tie(ix, iy, iz) = ind2sub_3D(i, size_x, size_y);
                voxel_counter += 1;

                // ------------------------------------------------------------
                // 1-jump neighbours
                // ------------------------------------------------------------
                if (ix > 0) {
                    j = sub2ind_3D(ix-1, iy, iz, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dX;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x) {
                    j = sub2ind_3D(ix+1, iy, iz, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dX;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iy > 0) {
                    j = sub2ind_3D(ix, iy-1, iz, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dY;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iy < end_y) {
                    j = sub2ind_3D(ix, iy+1, iz, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dY;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iz > 0) {
                    j = sub2ind_3D(ix, iy, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dZ;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iz < end_z) {
                    j = sub2ind_3D(ix, iy, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dZ;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }

                // ------------------------------------------------------------
                // 2-jump neighbours
                // ------------------------------------------------------------
                if (ix > 0 && iy > 0) {
                    j = sub2ind_3D(ix-1, iy-1, iz, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xy;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix > 0 && iy < end_y) {
                    j = sub2ind_3D(ix-1, iy+1, iz, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xy;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iy > 0) {
                    j = sub2ind_3D(ix+1, iy-1, iz, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xy;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iy < end_y) {
                    j = sub2ind_3D(ix+1, iy+1, iz, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xy;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iy > 0 && iz > 0) {
                    j = sub2ind_3D(ix, iy-1, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_yz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iy > 0 && iz < end_z) {
                    j = sub2ind_3D(ix, iy-1, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_yz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iy < end_y && iz > 0) {
                    j = sub2ind_3D(ix, iy+1, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_yz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (iy < end_y && iz < end_z) {
                    j = sub2ind_3D(ix, iy+1, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_yz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix > 0 && iz > 0) {
                    j = sub2ind_3D(ix-1, iy, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iz > 0) {
                    j = sub2ind_3D(ix+1, iy, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix > 0 && iz < end_z) {
                    j = sub2ind_3D(ix-1, iy, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iz < end_z) {
                    j = sub2ind_3D(ix+1, iy, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }

                // ------------------------------------------------------------
                // 3-jump neighbours
                // ------------------------------------------------------------
                if (ix > 0 && iy > 0 && iz > 0) {
                    j = sub2ind_3D(ix-1, iy-1, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xyz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix > 0 && iy > 0 && iz < end_z) {
                    j = sub2ind_3D(ix-1, iy-1, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xyz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix > 0 && iy < end_y && iz > 0) {
                    j = sub2ind_3D(ix-1, iy+1, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xyz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iy > 0 && iz > 0) {
                    j = sub2ind_3D(ix+1, iy-1, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xyz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix > 0 && iy < end_y && iz < end_z) {
                    j = sub2ind_3D(ix-1, iy+1, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xyz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iy > 0 && iz < end_z) {
                    j = sub2ind_3D(ix+1, iy-1, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xyz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iy < end_y && iz > 0) {
                    j = sub2ind_3D(ix+1, iy+1, iz-1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xyz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
                if (ix < end_x && iy < end_y && iz < end_z) {
                    j = sub2ind_3D(ix+1, iy+1, iz+1, size_x, size_y);

                    if (*(nii_rim_data + j) == 3) {
                        d = *(outerGM_dist_data + i) + dia_xyz;
                        if (d < *(outerGM_dist_data + j)
                            || *(outerGM_dist_data + j) == 0) {
                            *(outerGM_dist_data + j) = d;
                            *(outerGM_step_data + j) = grow_step + 1;
                            *(outerGM_id_data + j) = *(outerGM_id_data + i);
                            *(outerGM_prevstep_id_data + j) = i;
                        }
                    }
                }
            }
        }
        grow_step += 1;
    }
    if (mode_debug) {
        save_output_nifti(fin, "outerGM_step", outerGM_step, false);
        save_output_nifti(fin, "outerGM_dist", outerGM_dist, false);
        save_output_nifti(fin, "outerGM_id", outerGM_id, false);
    }

    // ========================================================================
    // Layers
    // ========================================================================
    cout << "\n  Start doing layers (equi-distant)..."   << endl;
    float x, y, z, wm_x, wm_y, wm_z, gm_x, gm_y, gm_z;

    for (uint32_t i = 0; i != nr_voxels; ++i) {
        if (*(nii_rim_data + i) == 3) {
            tie(x, y, z) = ind2sub_3D(i, size_x, size_y);
            tie(wm_x, wm_y, wm_z) = ind2sub_3D(*(innerGM_id_data + i),
                                               size_x, size_y);
            tie(gm_x, gm_y, gm_z) = ind2sub_3D(*(outerGM_id_data + i),
                                               size_x, size_y);

            // // Normalize distance
            // float dist1 = dist(x, y, z, wm_x, wm_y, wm_z, dX, dY, dZ);
            // float dist2 = dist(x, y, z, gm_x, gm_y, gm_z, dX, dY, dZ);
            // float norm_dist = dist1 / (dist1 + dist2);

            // Normalize distance (completely discrete)
            float dist1 = *(innerGM_dist_data + i);
            float dist2 = *(outerGM_dist_data + i);
            float total_dist = dist1 + dist2;
            *(thickness_data + i) = total_dist;
            float norm_dist = dist1 / total_dist;

            // Difference of normalized distances
            *(normdistdiff_data + i) = (dist1 - dist2) / total_dist;

            // Cast distances to integers as number of desired layers
            if (norm_dist != 0) {
                *(nii_layers_data + i) = ceil(nr_layers * norm_dist);
            } else {
                *(nii_layers_data + i) = 1;
            }

            // NOTE: for debugging purposes
            float dist3 = dist(wm_x, wm_y, wm_z, gm_x, gm_y, gm_z, dX, dY, dZ);
            *(err_dist_data + i) = (dist1 + dist2) - dist3;

            // Count inner and outer GM anchor voxels
            j = *(innerGM_id_data + i);
            *(hotspots_data + j) += 1;
            j = *(outerGM_id_data + i);
            *(hotspots_data + j) -= 1;
        }
    }
    save_output_nifti(fin, "thickness", thickness);
    save_output_nifti(fin, "layers_equidist", nii_layers);
    if (mode_debug) {
        save_output_nifti(fin, "hotspots", hotspots, false);
        save_output_nifti(fin, "disterror", err_dist, false);
        save_output_nifti(fin, "normdistdiff_equidist", normdistdiff, false);
    }

    // ========================================================================
    // Middle gray matter
    // ========================================================================
    cout << "\n  Start finding middle gray matter (equi-distant)..."   << endl;
    for (uint32_t i = 0; i != nr_voxels; ++i) {
        if (*(nii_rim_data + i) == 3) {
            // Check sign changes in normalized distance differences between
            // neighbouring voxels on a column path (a.k.a. streamline)
            if (*(normdistdiff_data + i) == 0) {
                *(midGM_data + i) = 1;
                *(midGM_id_data + i) = i;
            } else {
                float m = *(normdistdiff_data + i);
                float n;

                // Inner neighbour
                j = *(innerGM_prevstep_id_data + i);
                if (*(nii_rim_data + j) == 3) {
                    n = *(normdistdiff_data + j);
                    if (signbit(m) - signbit(n) != 0) {
                        if (abs(m) < abs(n)) {
                            *(midGM_data + i) = 1;
                            *(midGM_id_data + i) = i;
                        } else if (abs(m) > abs(n)) {  // Closer to prev. step
                            *(midGM_data + j) = 1;
                            *(midGM_id_data + j) = j;
                        } else {  // Equal +/- normalized distance
                            *(midGM_data + i) = 1;
                            *(midGM_id_data + i) = i;
                            *(midGM_data + j) = 1;
                            *(midGM_id_data + j) = i;  // On purpose
                        }
                    }
                }

                // Outer neighbour
                j = *(outerGM_prevstep_id_data + i);
                if (*(nii_rim_data + j) == 3) {
                    n = *(normdistdiff_data + j);
                    if (signbit(m) - signbit(n) != 0) {
                        if (abs(m) < abs(n)) {
                            *(midGM_data + i) = 1;
                            *(midGM_id_data + i) = i;
                        } else if (abs(m) > abs(n)) {  // Closer to prev. step
                            *(midGM_data + j) = 1;
                            *(midGM_id_data + j) = j;
                        } else {  // Equal +/- normalized distance
                            *(midGM_data + i) = 1;
                            *(midGM_id_data + i) = i;
                            *(midGM_data + j) = 1;
                            *(midGM_id_data + j) = i;  // On purpose
                        }
                    }
                }
            }
        }
    }
    save_output_nifti(fin, "midGM_equidist", midGM, true);

    // ========================================================================
    // Columns
    // ========================================================================
    for (uint32_t i = 0; i != nr_voxels; ++i) {
        if (*(nii_rim_data + i) == 3) {
            // Approximate curvature measurement per column/streamline
            j = *(innerGM_id_data + i);
            k = *(outerGM_id_data + i);  // These values are negative
            *(curvature_data + i) = *(hotspots_data + j) + *(hotspots_data + k);
            *(curvature_data + i) /=
                max(*(hotspots_data + j), -*(hotspots_data + k));  // normalize

            // Re-assign mid-GM id based on curvature
            if (*(curvature_data + i) >= 0) {  // Gyrus
                *(nii_columns_data + i) = j;
            } else {
                *(nii_columns_data + i) = k;
            }

            // MiddleGM ids are used to find centroids in the next step
            if (*(midGM_data + i) == 1) {
                *(midGM_id_data + i) = *(nii_columns_data + i);
            }
        }
    }
    if (mode_debug) {
        save_output_nifti(fin, "curvature_init", curvature, false);
    }

    // ========================================================================
    // Find Middle Gray Matter centroids
    // ========================================================================
    // NOTE(Faruk): I am a bit sluggish with memory usage here. Might optimize
    // later by switching nifti images to vectors.
    nifti_image* coords_x = copy_nifti_as_float32(nii_rim);
    float* coords_x_data = static_cast<float*>(coords_x->data);
    nifti_image* coords_y = copy_nifti_as_float32(nii_rim);
    float* coords_y_data = static_cast<float*>(coords_y->data);
    nifti_image* coords_z = copy_nifti_as_float32(nii_rim);
    float* coords_z_data = static_cast<float*>(coords_z->data);
    nifti_image* coords_count = copy_nifti_as_int32(nii_rim);
    int32_t* coords_count_data = static_cast<int32_t*>(coords_count->data);
    nifti_image* centroid = copy_nifti_as_int32(nii_rim);
    int32_t* centroid_data = static_cast<int32_t*>(centroid->data);
    nifti_image* midGM_centroid_id = copy_nifti_as_int32(nii_columns);
    int32_t* midGM_centroid_id_data = static_cast<int32_t*>(midGM_centroid_id->data);

    for (uint32_t i = 0; i != nr_voxels; ++i) {
        *(coords_x_data + i) = 0;
        *(coords_y_data + i) = 0;
        *(coords_z_data + i) = 0;
        *(coords_count_data + i) = 0;
        *(centroid_data + i) = 0;
    }

    // Sum x, y, z coordinates of same-column middle GM voxels
    for (uint32_t i = 0; i != nr_voxels; ++i) {
        if (*(midGM_data + i) == 1) {
            tie(x, y, z) = ind2sub_3D(i, size_x, size_y);
            j = *(midGM_id_data + i);  // used to determine storage voxel
            *(coords_x_data + j) += x;
            *(coords_y_data + j) += y;
            *(coords_z_data + j) += z;
            *(coords_count_data + j) += 1;
        }
    }
    // Divide summed coordinates to find centroid
    for (uint32_t i = 0; i != nr_voxels; ++i) {
        if (*(coords_count_data + i) != 0) {
            // Assign centroid id in place of inner/outer border voxel id
            x = floor(*(coords_x_data + i) / *(coords_count_data + i));
            y = floor(*(coords_y_data + i) / *(coords_count_data + i));
            z = floor(*(coords_z_data + i) / *(coords_count_data + i));
            j = sub2ind_3D(x, y, z, size_x, size_y);
            *(centroid_data + i) = j;
        }
    }
    // Map new centroid IDs to columns/streamlines
    for (uint32_t i = 0; i != nr_voxels; ++i) {
        if (*(nii_rim_data + i) == 3) {
            j = *(nii_columns_data + i);
            *(midGM_centroid_id_data + i) = *(centroid_data + j);

            if (*(midGM_data + i) == 1) {  // Update Mid GM id
                *(midGM_id_data + i) = *(centroid_data + j);
            }
        }
    }
    if (mode_debug) {
        save_output_nifti(fin, "midGM_equidist_id", midGM_id, false);
        save_output_nifti(fin, "columns", midGM_centroid_id, false);
    }

    // ========================================================================
    // Update curvature along column/streamline based on midGM curvature
    // ========================================================================
    for (uint32_t i = 0; i != nr_voxels; ++i) {
        if (*(nii_rim_data + i) == 3) {
            j = *(midGM_centroid_id_data + i);
            *(curvature_data + i) = *(curvature_data + j);
        }
    }
    save_output_nifti(fin, "curvature", curvature, true);

    // ========================================================================
    // Equi-volume layers
    // ========================================================================
    if (mode_equivol) {
        cout << "\n  Start layers (equi-volume)..."   << endl;

        nifti_image* hotspots_i = copy_nifti_as_float32(nii_rim);
        float* hotspots_i_data = static_cast<float*>(hotspots_i->data);
        nifti_image* hotspots_o = copy_nifti_as_float32(nii_rim);
        float* hotspots_o_data = static_cast<float*>(hotspots_o->data);

        for (uint32_t i = 0; i != nr_voxels; ++i) {
            *(hotspots_i_data + i) = 0;
            *(hotspots_o_data + i) = 0;
        }

        for (uint32_t i = 0; i != nr_voxels; ++i) {
            if (*(nii_rim_data + i) == 3) {
                // Find inner/outer anchors
                j = *(innerGM_id_data + i);
                k = *(outerGM_id_data + i);

                // Count how many voxels fall inner and outer shells from MidGM
                if (*(curvature_data + i) < 0) {
                    if (*(normdistdiff_data + i) <= 0) {
                        *(hotspots_i_data + k) += 1;
                    }
                    if (*(normdistdiff_data + i) >= 0) {
                        *(hotspots_o_data + k) += 1;
                    }
                }
                if (*(curvature_data + i) > 0) {
                    if (*(normdistdiff_data + i) <= 0) {
                        *(hotspots_i_data + j) += 1;
                    }
                    if (*(normdistdiff_data + i) >= 0) {
                        *(hotspots_o_data + j) += 1;
                    }
                }
            }
        }
        if (mode_debug) {
            save_output_nifti(fin, "hotspots_in", hotspots_i, false);
            save_output_nifti(fin, "hotspots_out", hotspots_o, false);
        }

        // --------------------------------------------------------------------
        // Compute equi-volume factors
        // --------------------------------------------------------------------
        nifti_image* equivol_factors = copy_nifti_as_float32(nii_rim);
        float* equivol_factors_data = static_cast<float*>(equivol_factors->data);
        for (uint32_t i = 0; i != nr_voxels; ++i) {
            *(equivol_factors_data + i) = 0;
        }

        float w;
        for (uint32_t i = 0; i != nr_voxels; ++i) {
            if (*(nii_rim_data + i) == 3) {
                // Find mass at each end of the given column
                j = *(innerGM_id_data + i);
                k = *(outerGM_id_data + i);
                if (*(curvature_data + i) == 0) {
                    w = 0.5;
                } else if (*(curvature_data + i) < 0) {
                    w = *(hotspots_i_data + k)
                        / (*(hotspots_i_data + k) + *(hotspots_o_data + k));
                } else if (*(curvature_data + i) > 0) {
                    w = *(hotspots_i_data + j)
                        / (*(hotspots_i_data + j) + *(hotspots_o_data + j));
                }
                *(equivol_factors_data + i) = w;
            }
        }

        if (mode_debug) {
            save_output_nifti(fin, "equivol_factors", equivol_factors, false);
        }

        // --------------------------------------------------------------------
        // Smooth equi-volume factors for seamless transitions
        // --------------------------------------------------------------------
        nifti_image* smooth = copy_nifti_as_float32(curvature);
        float* smooth_data = static_cast<float*>(smooth->data);
        for (uint32_t i = 0; i != nr_voxels; ++i) {
            *(smooth_data + i) = 0;
        }

        // Pre-compute weights
        float FWHM_val = 1;  // TODO(Faruk): Might tweak this one
        float w_0 = gaus(0, FWHM_val);
        float w_dX = gaus(dX, FWHM_val);
        float w_dY = gaus(dY, FWHM_val);
        float w_dZ = gaus(dZ, FWHM_val);

        for (uint16_t n = 0; n != iter_smooth; ++n) {
            for (uint32_t i = 0; i != nr_voxels; ++i) {
                if (*(nii_rim_data + i) == 3) {
                    tie(ix, iy, iz) = ind2sub_3D(i, size_x, size_y);
                    float new_val = 0, total_weight = 0;

                    // Start with the voxel itself
                    new_val += *(equivol_factors_data + i) * w_0;
                    total_weight += w_0;

                    // --------------------------------------------------------
                    // 1-jump neighbours
                    // --------------------------------------------------------
                    if (ix > 0) {
                        j = sub2ind_3D(ix-1, iy, iz, size_x, size_y);
                        if (*(nii_rim_data + j) == 3) {
                            new_val += *(equivol_factors_data + j) * w_dX;
                            total_weight += w_dX;
                        }
                    }
                    if (ix < end_x) {
                        j = sub2ind_3D(ix+1, iy, iz, size_x, size_y);
                        if (*(nii_rim_data + j) == 3) {
                            new_val += *(equivol_factors_data + j) * w_dX;
                            total_weight += w_dX;
                        }
                    }
                    if (iy > 0) {
                        j = sub2ind_3D(ix, iy-1, iz, size_x, size_y);
                        if (*(nii_rim_data + j) == 3) {
                            new_val += *(equivol_factors_data + j) * w_dY;
                            total_weight += w_dY;
                        }
                    }
                    if (iy < end_y) {
                        j = sub2ind_3D(ix, iy+1, iz, size_x, size_y);
                        if (*(nii_rim_data + j) == 3) {
                            new_val += *(equivol_factors_data + j) * w_dY;
                            total_weight += w_dY;
                        }
                    }
                    if (iz > 0) {
                        j = sub2ind_3D(ix, iy, iz-1, size_x, size_y);
                        if (*(nii_rim_data + j) == 3) {
                            new_val += *(equivol_factors_data + j) * w_dZ;
                            total_weight += w_dZ;
                        }
                    }
                    if (iz < end_z) {
                        j = sub2ind_3D(ix, iy, iz+1, size_x, size_y);
                        if (*(nii_rim_data + j) == 3) {
                            new_val += *(equivol_factors_data + j) * w_dZ;
                            total_weight += w_dZ;
                        }
                    }
                    *(smooth_data + i) = new_val / total_weight;
                }
            }
            // Swap image data
            for (uint32_t i = 0; i != nr_voxels; ++i) {
                *(equivol_factors_data + i) = *(smooth_data + i);
            }
        }
        if (mode_debug) {
            save_output_nifti(fin, "equivol_factors_smooth", smooth, false);
        }

        // --------------------------------------------------------------------
        // Apply equi-volume factors
        // --------------------------------------------------------------------
        float d1_new, d2_new, a, b;
        for (uint32_t i = 0; i != nr_voxels; ++i) {
            if (*(nii_rim_data + i) == 3) {
                // Find normalized distances from a given point on a column
                float dist1 = *(innerGM_dist_data + i) / *(thickness_data + i);
                float dist2 = *(outerGM_dist_data + i) / *(thickness_data + i);

                a = *(equivol_factors_data + i);
                b = 1 - a;

                // Perturb using masses to modify distances in simplex space
                tie(d1_new, d2_new) = simplex_perturb_2D(dist1, dist2, a, b);

                // Difference of normalized distances (used in finding midGM)
                *(normdistdiff_data + i) = d1_new - d2_new;

                // Cast distances to integers as number of desired layers
                if (d1_new != 0 && isfinite(d1_new)) {
                    *(nii_layers_data + i) =  ceil(nr_layers * d1_new);
                } else {
                    *(nii_layers_data + i) = 1;
                }
            }
        }
        save_output_nifti(fin, "layers_equivol", nii_layers);
        if (mode_debug) {
            save_output_nifti(fin, "normdistdiff_equivol", normdistdiff, false);
        }

        // ====================================================================
        // Middle gray matter for equi-volume
        // ====================================================================
        cout << "\n  Start finding middle gray matter (equi-volume)..." << endl;
        for (uint32_t i = 0; i != nr_voxels; ++i) {
            *(midGM_data + i) = 0;
            *(midGM_id_data + i) = 0;
        }

        for (uint32_t i = 0; i != nr_voxels; ++i) {
            if (*(nii_rim_data + i) == 3) {
                // Check sign changes in normalized distance differences between
                // neighbouring voxels on a column path (a.k.a. streamline)
                if (*(normdistdiff_data + i) == 0) {
                    *(midGM_data + i) = 1;
                    *(midGM_id_data + i) = i;
                } else {
                    float m = *(normdistdiff_data + i);
                    float n;

                    // Inner neighbour
                    j = *(innerGM_prevstep_id_data + i);
                    if (*(nii_rim_data + j) == 3) {
                        n = *(normdistdiff_data + j);
                        if (signbit(m) - signbit(n) != 0) {
                            if (abs(m) < abs(n)) {
                                *(midGM_data + i) = 1;
                                *(midGM_id_data + i) = i;
                            } else if (abs(m) > abs(n)) {  // Closer to prev. step
                                *(midGM_data + j) = 1;
                                *(midGM_id_data + j) = j;
                            } else {  // Equal +/- normalized distance
                                *(midGM_data + i) = 1;
                                *(midGM_id_data + i) = i;
                                *(midGM_data + j) = 1;
                                *(midGM_id_data + j) = i;  // On purpose
                            }
                        }
                    }

                    // Outer neighbour
                    j = *(outerGM_prevstep_id_data + i);
                    if (*(nii_rim_data + j) == 3) {
                        n = *(normdistdiff_data + j);
                        if (signbit(m) - signbit(n) != 0) {
                            if (abs(m) < abs(n)) {
                                *(midGM_data + i) = 1;
                                *(midGM_id_data + i) = i;
                            } else if (abs(m) > abs(n)) {  // Closer to prev. step
                                *(midGM_data + j) = 1;
                                *(midGM_id_data + j) = j;
                            } else {  // Equal +/- normalized distance
                                *(midGM_data + i) = 1;
                                *(midGM_id_data + i) = i;
                                *(midGM_data + j) = 1;
                                *(midGM_id_data + j) = i;  // On purpose
                            }
                        }
                    }
                }
            }
        }
        save_output_nifti(fin, "midGM_equivol", midGM, true);
    }

    // ========================================================================
    // TODO(Faruk): Might use bspline weights to smooth curvature maps a bit.
    // TODO(Faruk): Might be better to use step 1 id's to define columns.

    cout << "\n  Finished." << endl;
    return 0;
}
