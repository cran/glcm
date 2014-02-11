#include <RcppArmadillo.h>
using namespace arma;

// Define a pointer to a texture function that will be used to map selected 
// co-occurrence statistics to the below texture calculation functions.
typedef double (*pfunc)(mat, mat, mat, double, double);

double text_mean(mat pij, mat imat, mat jmat, double mean_haralick, double ENVI_mean) {
    // Defined as in Haralick as mean of mux and muy
    return(mean_haralick);
}

double text_mean_ENVI(mat pij, mat imat, mat jmat, double mean_haralick, double ENVI_mean) {
    // Defined as in EXELIS ENVI (as simple mean over processing window)
    return(ENVI_mean);
}
            
double text_variance(mat pij, mat imat, mat jmat, double mean_haralick, double ENVI_mean) {
    // Defined as in Haralick, 1973, page 619 (equation 4)
    return(accu(square(imat - mean_haralick) % pij));
}

double text_variance_ENVI(mat pij, mat imat, mat jmat, double mean_haralick, double ENVI_mean) {
    // Defined as in EXELIS ENVI
    return(accu(square(imat - ENVI_mean) % pij) - 1);
}

double text_homogeneity(mat pij, mat imat, mat jmat, double mean_haralick, double ENVI_mean) {
    // Defined as in Haralick, 1973, page 619 (equation 5)
    return(accu(pij / (1 + square(imat - jmat))));
}

double text_contrast(mat pij, mat imat, mat jmat, double mean_haralick, double ENVI_mean) {
    // Defined as in Haralick, 1973, page 619 (equation 2)
    return(accu(pij % square(imat - jmat)));
}

double text_dissimilarity(mat pij, mat imat, mat jmat, double mean_haralick, double ENVI_mean) {
    //TODO: Find source for dissimilarity
    return(accu(pij % abs(imat - jmat)));
}

double text_entropy(mat pij, mat imat, mat jmat, double mean_haralick, double ENVI_mean) {
    // Defined as in Haralick, 1973, page 619 (equation 9)
    mat pij_log(pij);
    pij_log = mat(pij);
    pij_log(find(pij_log)) = log(pij_log(find(pij_log)));
    return(-accu(pij % pij_log));
}

double text_second_moment(mat pij, mat imat, mat jmat, double mean_haralick, double ENVI_mean) {
    // Defined as in Haralick, 1973, page 619
    return(accu(square(pij)));
}

double text_correlation(mat pij, mat imat, mat jmat, double mean_haralick, double ENVI_mean) {
    // Defined as in Gonzalez and Woods, 2009, page 832, also follows ENVI 
    // convention of using mr and mc equal to the sum rather than (as in 
    // Haralick 1973, eqn 3 on page 619) the mean of the pij by rows and columns
    double sigc, sigr, mr, mc;
    mr = sum(linspace<colvec>(1, pij.n_cols, pij.n_cols) % sum(pij, 1));
    mc = sum(linspace<rowvec>(1, pij.n_rows, pij.n_rows) % sum(pij, 0));
    // Calculate sigr and sigc (measures of row and column std deviation)
    sigr = sqrt(sum(square(linspace<colvec>(1, pij.n_cols, pij.n_cols) - mr) % sum(pij, 1)));
    sigc = sqrt(sum(square(linspace<rowvec>(1, pij.n_rows, pij.n_rows) - mc) % sum(pij, 0)));
    return((accu(imat % jmat % pij) - mr * mc) / (sigr * sigc));
}

//' Calculates a glcm texture for use in the glcm.R script
//'
//' This function is called by the \code{\link{glcm}} function. It is 
//' not intended to be used directly.
//'
//' @param rast a matrix containing the pixels to be used in the texture
//' calculation
//' @param n_grey number of grey levels to use in texture calculation
//' @param window_dims 2 element list with row and column dimensions of the
//' texture window
//' @param shift a length 2 vector with the number of cells to shift when
//' computing co-ocurrency matrices
//' @param statistics a list of strings naming the texture statistics to 
//' calculate
//' @param na_opt one of "ignore", "center", or "any"
//' @param na_val what value to use to fill missing values on edges or where
//' necessary due to chosen na_opt value
//' @return a list of length equal to the length of the \code{statistics} input 
//' parameter, containing the selected textures measures
// [[Rcpp::export]]
arma::cube calc_texture(arma::mat rast,
        int n_grey, arma::vec window_dims, arma::vec shift,
        Rcpp::CharacterVector statistics, std::string na_opt, double na_val) {
    mat imat(n_grey, n_grey);
    mat jmat(n_grey, n_grey);
    mat base_window(window_dims(0), window_dims(1));
    mat offset_window(window_dims(0), window_dims(1));
    mat pij(n_grey, n_grey);
    vec base_ul(2), offset_ul(2), center_coord(2);
    double mean_haralick, ENVI_mean;

    // textures cube will hold the calculated texture statistics
    cube textures(rast.n_rows, rast.n_cols, statistics.size());

    std::map<std::string, double (*)(mat, mat, mat, double, double)> stat_func_map;
    stat_func_map["mean"] = text_mean;
    stat_func_map["mean_ENVI"] = text_mean_ENVI;
    stat_func_map["variance"] = text_variance;
    stat_func_map["variance_ENVI"] = text_variance_ENVI;
    stat_func_map["homogeneity"] = text_homogeneity;
    stat_func_map["contrast"] = text_contrast;
    stat_func_map["dissimilarity"] = text_dissimilarity;
    stat_func_map["entropy"] = text_entropy;
    stat_func_map["second_moment"] = text_second_moment;
    stat_func_map["correlation"] = text_correlation;

    // Calculate the base upper left (ul) coords and offset upper left coords 
    // as row, column with zero based indices.
    base_ul = vec("0 0");
    if (shift[0] < 0) {
        base_ul[0] = base_ul[0] + abs(shift[0]);
    }
    if (shift[1] < 0) {
        base_ul[1] = base_ul[1] + abs(shift[1]);
    }
    offset_ul = base_ul + shift;
    center_coord = base_ul + floor(window_dims / 2);
     
    // Make a matrix of i's and a matrix of j's to be used in the below matrix 
    // calculations. These matrices are the same shape as pij with the entries 
    // equal to the i indices of each cell (for the imat matrix, which is 
    // indexed over the rows) or the j indices of each cell (for the jmat 
    // matrix, which is indexed over the columns). Note that linspace<mat> 
    // makes a column vector.
    //imat = repmat(linspace<colvec>(1, pij.n_rows, pij.n_rows), 1, 
    //pij.n_cols);
    //jmat = repmat(linspace<rowvec>(1, pij.n_cols, pij.n_cols), pij.n_rows, 
    //1);
    imat = repmat(linspace<vec>(1, pij.n_rows, pij.n_rows), 1, pij.n_cols);
    jmat = trans(imat);

    for(unsigned row=0; row < (rast.n_rows - abs(shift(0)) - ceil(window_dims(0)/2)); row++) {
        // if (row %250 == 0 ) {
        //     Rcpp::Rcout << "Row: " << row << std::endl;
        // }
        for(unsigned col=0; col < (rast.n_cols - abs(shift(1)) - ceil(window_dims(1)/2)); col++) {
            base_window = rast.submat(row + base_ul(0),
                                      col + base_ul(1),
                                      row + base_ul(0) + window_dims(0) - 1,
                                      col + base_ul(1) + window_dims(1) - 1);
            offset_window = rast.submat(row + offset_ul(0),
                                        col + offset_ul(1),
                                        row + offset_ul(0) + window_dims(0) - 1,
                                        col + offset_ul(1) + window_dims(1) - 1);
            pij.fill(0);

            if (na_opt == "any") {
                // Return NA for all textures within this window if there are 
                // any NA values within the base_window or the offset_window
                bool na_flag=false;
                for(unsigned i=0; i < base_window.n_elem; i++) {
                    if ((!arma::is_finite(base_window(i))) || (!arma::is_finite(offset_window(i)))) {
                        for(signed s=0; s < statistics.size(); s++) {
                            textures(row + center_coord(0),
                                     col + center_coord(1), s) = na_val;
                        }
                        na_flag = true;
                        break;
                    }
                }
                if (na_flag) continue;
            } else if (na_opt == "center") {
                // Return NA for all textures within this window if the center 
                // value is an NA
                if (!arma::is_finite(base_window(center_coord(0), center_coord(1)))) {
                    for(signed s=0; s < statistics.size(); s++) {
                        textures(row + center_coord(0),
                                 col + center_coord(1), s) = na_val;
                    }
                    continue;
                }
            }

            for(unsigned i=0; i < base_window.n_elem; i++) {
                if (!(arma::is_finite(base_window(i)) || arma::is_finite(offset_window(i)))) {
                    // This will execute only if there is an NA in the window 
                    // AND na_opt is set to "ignore" or "center"
                    continue;
                }
                // Subtract one from the below indices to correct for row and 
                // col indices starting at 0 in C++ versus 1 in R.
                pij(base_window(i) - 1, offset_window(i) - 1)++;
            }
            pij = pij / base_window.n_elem;

            mean_haralick = (mean(linspace<colvec>(1, pij.n_rows, pij.n_rows) %
                        sum(pij, 1)) +
                    mean(linspace<rowvec>(1, pij.n_rows, pij.n_rows) %
                        sum(pij, 0))) / 2;
            ENVI_mean = mean(vectorise(base_window) - 1);

            // Loop over the selected statistics, using the stat_func_map map 
            // to map each selected statistic to the appropriate texture 
            // function.
            for(signed i=0; i < statistics.size(); i++) {
                pfunc f = stat_func_map[Rcpp::as<std::string>(statistics(i))];
                textures(row + center_coord(0),
                         col + center_coord(1), i) = (*f)(pij, imat, jmat, mean_haralick, ENVI_mean);
            }

        }
    }

    // The below loops fill in border areas with nan values in areas where 
    // textures cannot be calculated due to edge effects.
    
    // Fill nan values on left border
    for(unsigned row=0; row < rast.n_rows; row++) {
        for(unsigned col=0; col < center_coord(1); col++) {
            for(signed i=0; i < statistics.size(); i++) {
                textures(row, col, i) = na_val;
            }
        }
    }

    // Fill nan values on top border
    for(unsigned row=0; row < center_coord(0); row++) {
        for(unsigned col=0; col < rast.n_cols; col++) {
            for(signed i=0; i < statistics.size(); i++) {
                textures(row, col, i) = na_val;
            }
        }
    }

    // Fill nan values on right border
    for(unsigned row=0; row < rast.n_rows; row++) {
        for(unsigned col=(rast.n_cols - abs(shift(1)) - ceil(window_dims(1)/2)) + 1; col < rast.n_cols; col++) {
            for(signed i=0; i < statistics.size(); i++) {
                textures(row, col, i) = na_val;
            }
        }
    }

    // Fill nan values on bottom border
    for(unsigned row=(rast.n_rows - abs(shift(0)) - ceil(window_dims(0)/2)) + 1; row < rast.n_rows; row++) {
        for(unsigned col=0; col < rast.n_cols; col++) {
            for(signed i=0; i < statistics.size(); i++) {
                textures(row, col, i) = na_val;
            }
        }
    }

    return(textures);
}