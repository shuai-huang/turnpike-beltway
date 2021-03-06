#include<chrono>
#include "uDGP.h"

uDGP::uDGP() {
    M=0;
}

void uDGP::SetOutputFile(char* output_file) {
    output_ite_file = output_file;
}

void uDGP::SetInitFile(char* init_file) {
    smp_pos_init_file = init_file;
}

double uDGP::NormalCdf(double x_val, double mu, double sigma) {
    return 0.5*(1+erf((x_val-mu)/(sigma*sqrt(2))));
}

//bool uDGP::mypair_des_com ( const mypair& l, const mypair& r) { return l.second > r.second; }

void uDGP::SetData(DataReader* data_reader) {

    raw_distribution = data_reader->getDistributionData();
    
    max_distance = 0;
    for (int i=0; i<num_raw_uq_distance; i++) {
        double raw_val = raw_distribution[i][0];
        
        if (raw_val>max_distance) {
            max_distance = raw_val;
        }
    }
    domain_sz = max_distance + 2*tau;
    M = round(max_distance/min_space_unit) + 1 + 2*round(tau/min_space_unit); 

    // convert raw data to uq_distance and uq_distribution, convolution with the noise distribution
    // this is used to determine the possible locations
    for (int i=0; i<num_raw_uq_distance; i++) {
        
        double raw_val = raw_distribution[i][0];
        if (raw_val==0) {   // only keep track of distances between two different points
            continue;
        }
        
        int raw_distance_idx = round(raw_val/min_space_unit);

        vector<int> distance_val_approx_seq;
        distance_val_approx_seq.push_back(raw_distance_idx);
        
        
        for (int j=1; j<=M; j++) {
            if (j*min_space_unit>tau) {
                break;
            }
            if (((raw_distance_idx-j)*min_space_unit)>=0) {
                distance_val_approx_seq.push_back(raw_distance_idx-j);
            }

            if (((raw_distance_idx+j)*min_space_unit)<=domain_sz) {
                distance_val_approx_seq.push_back(raw_distance_idx+j);
            }
        }
        
        for (int j=0; j<distance_val_approx_seq.size(); j++) {
            int distance_val_approx = distance_val_approx_seq[j];
            
            if (all_distance_diff.find(distance_val_approx)==all_distance_diff.end()) {  // contains all the distances to be considered
                all_distance_diff[distance_val_approx] = 0;
             }
        }
    }
    
}

double uDGP::ComputeEstDbt( VectorXd* smp_vec, int distance_val) {

    double est_dbt_val = 0;
    
    //est_dbt_val += ( (*smp_vec).segment(0, M-distance_val).array() * (*smp_vec).segment(distance_val, M-distance_val).array() ).sum();
    int j_idx, k_idx;
    for (int i=0; i<valid_idx_vec.size(); i++) {
        j_idx = valid_idx_vec[i];
        k_idx = j_idx + distance_val;
        if (k_idx<=M-1) {
            est_dbt_val += (*smp_vec)(j_idx) * (*smp_vec)(k_idx);
        }
    }

    return est_dbt_val;
}

void uDGP::ComputeEstProj(VectorXd* smp_vec, VectorXd* smp_vec_proj, double mut_factor, int distance_val) {

    //(*smp_vec_proj).segment(0, M-distance_val) += mut_factor * (*smp_vec).segment(distance_val, M-distance_val);
    //(*smp_vec_proj).segment(distance_val, M-distance_val) += mut_factor * (*smp_vec).segment(0, M-distance_val);
    int j_idx, k_idx, l_idx;
    for (int i=0; i<valid_idx_vec.size(); i++) {
        j_idx = valid_idx_vec[i];
        k_idx = j_idx + distance_val;
        l_idx = j_idx - distance_val;
        if (k_idx<=M-1) {
            (*smp_vec_proj)(j_idx) += mut_factor * (*smp_vec)(k_idx);
        }
        if (l_idx>=0) {
            (*smp_vec_proj)(j_idx) += mut_factor * (*smp_vec)(l_idx);
        }
    }
    
}

void uDGP::SetMeasureMatrix() {

    // set measurement matrix and the all_distribution
    
    // set the two anchor points indices corresponding to the two outmost points
    anchor_one = round(tau/min_space_unit); // the index starts from 0, there are round(tau/min_space_unit) segments to the left
    anchor_two = anchor_one + round(max_distance/min_space_unit);

    int dist_val_tmp_1, dist_val_tmp_2;
    
    // Put the anchor point positions
    // Find valid idx positions
    valid_idx_pos = VectorXd::Zero(M);
    for (int i=0; i<(anchor_one+round(tau/min_space_unit)+1); i++) {
        anchor_one_seq[i]=0;
        valid_idx_pos[i]=1;
        valid_idx_vec.push_back(i);
    }
    for (int i=anchor_two-round(tau/min_space_unit); i<(anchor_two+round(tau/min_space_unit)+1); i++) {
        anchor_two_seq[i]=0;
        valid_idx_pos[i]=1;
        valid_idx_vec.push_back(i);
    }
    
    for (int i=anchor_one+1; i<anchor_two; i++) {
        
        // distance to first anchor point
        dist_val_tmp_1 = abs(i-anchor_one);
        
        // distance to second anchor point
        dist_val_tmp_2 = abs(i-anchor_two);

        if ( ( all_distance_diff.find(dist_val_tmp_1)!=all_distance_diff.end() ) && ( all_distance_diff.find(dist_val_tmp_2)!=all_distance_diff.end() ) ) {
            if ( (anchor_one_seq.find(i)==anchor_one_seq.end()) && (anchor_two_seq.find(i)==anchor_two_seq.end()) ) {
                valid_idx_pos[i]=1;
                valid_idx_vec.push_back(i);
                valid_idx_vec_exclude.push_back(i);
            }
        }
    }

    num_pos = valid_idx_vec.size();
    cout<<"Number of valid index:     "<<valid_idx_vec.size()<<endl;
    cout<<"Number of anchor one:      "<<anchor_one_seq.size()<<endl;
    cout<<"Number of anchor two:      "<<anchor_two_seq.size()<<endl;

    
    // Find all possible distances in the domain
    all_distribution[0]=0;
    all_distance.push_back(0);
    
    int distance_val_tmp;
    
    for (int i=0; i<num_pos; i++) {
        for (int j=i+1; j<num_pos; j++) {
            distance_val_tmp = abs(valid_idx_vec[i]-valid_idx_vec[j]);

	    // In the noiseless case, only consider the distances that appear in the measurements
            if (all_distance_diff.find(distance_val_tmp)==all_distance_diff.end()) {
                continue;
            }
            
            if ( all_distribution.find(distance_val_tmp)==all_distribution.end() ) {
                all_distribution[distance_val_tmp]=0;
                all_distance.push_back(distance_val_tmp);
            }
        }
    }
    
    cout<<"Valid distance size:       "<<all_distance.size()<<endl;    
    cout<<"Distribution size:         "<<all_distribution.size()<<endl;
    
    // approximate the oracle distance distribution    
    for (int i=0; i<num_raw_uq_distance; i++) {
        
        double raw_val = raw_distribution[i][0];
        
        map<int, double> prob_cov;
        double prob_cov_sum = 0;

        int raw_distance_idx = round(raw_val/min_space_unit);
        
        vector<int> distance_val_approx_seq;
        if (all_distribution.find(raw_distance_idx)!=all_distribution.end()) {
            distance_val_approx_seq.push_back(raw_distance_idx);
            if (raw_distance_idx==0) {
                prob_cov[raw_distance_idx] = 2*(NormalCdf( (raw_distance_idx+0.5)*min_space_unit, raw_val, sigma) - NormalCdf( (raw_distance_idx)*min_space_unit, raw_val, sigma) );
            } else {
                prob_cov[raw_distance_idx] = NormalCdf( (raw_distance_idx+0.5)*min_space_unit, raw_val, sigma) - NormalCdf( (raw_distance_idx-0.5)*min_space_unit, raw_val, sigma);
            }
            prob_cov_sum += prob_cov[raw_distance_idx];
        }
        
        for (int j=1; j<M; j++) {
            if (j*min_space_unit>tau) {
                break;
            }
            
            if (all_distribution.find(raw_distance_idx-j)!=all_distribution.end()) {
                distance_val_approx_seq.push_back(raw_distance_idx-j);
                prob_cov[raw_distance_idx-j] = NormalCdf( (raw_distance_idx-j+0.5)*min_space_unit, raw_val, sigma) - NormalCdf( (raw_distance_idx-j-0.5)*min_space_unit, raw_val, sigma );
                
                prob_cov_sum += prob_cov[raw_distance_idx-j];
            }

            if (all_distribution.find(raw_distance_idx+j)!=all_distribution.end()) {
                distance_val_approx_seq.push_back(raw_distance_idx+j);
                prob_cov[raw_distance_idx+j] = NormalCdf( (raw_distance_idx+j+0.5)*min_space_unit, raw_val, sigma) - NormalCdf( (raw_distance_idx+j-0.5)*min_space_unit, raw_val, sigma );

                prob_cov_sum += prob_cov[raw_distance_idx+j];
            }
        }
	prob_cov_sum = 1;
        
        for (int j=0; j<distance_val_approx_seq.size(); j++) {
            int distance_val_approx = distance_val_approx_seq[j];
            
            prob_cov[distance_val_approx] = prob_cov[distance_val_approx]/prob_cov_sum;

            double distribution_val_tmp = prob_cov[distance_val_approx]*raw_distribution[i][1];
            
            all_distribution[distance_val_approx] += distribution_val_tmp;
        }
    }
    
    cout<<"No. of raw uq sq distance: "<<raw_distribution.size()<<endl;
    cout<<"Distribution size:         "<<all_distribution.size()<<"\t"<<all_distance.size()<<endl;
   
    // sort the all_distance
    sort(all_distance.begin(), all_distance.end());

    
    // count the number of blocks for each unique distance, this is for the multi-threading program
    for (int i=0; i<all_distance.size(); i++) {
        all_block_count.push_back({all_distance[i], 1}); 
    }
    // sort the all_distance according to the block_count
    sort(all_block_count.begin(), all_block_count.end(), []( const mypair& l, const mypair& r) { return l.second > r.second; });
    
    num_thread_assign = num_thread > all_distance.size() ? all_distance.size() : num_thread;

    vector<int> all_partition_sz(num_thread_assign,0);
    for (int i=0; i<all_block_count.size(); i++) {
            int idx_min=distance( all_partition_sz.begin(), min_element(all_partition_sz.begin(), all_partition_sz.end()) );
            all_partition[idx_min].push_back(all_block_count[i].first);
            all_partition_sz[idx_min] += all_block_count[i].second;
    }
    
     // initialize the estimated sq distributin here ***
    for (int i=0; i<all_distance.size(); i++) {
        est_distribution[all_distance[i]] = 0;   
    }

    cout<<"Multithreading assignment stats: ";
    for (int i=0; i<all_partition_sz.size(); i++) {
        cout<<all_partition_sz[i]<<"\t";
    }
    cout<<endl;
    
    cout<<"Setting measurement matrices finished."<<endl;

}

void uDGP::Initialization() {
    
    smp_pos_init = VectorXd::Zero(M);

    if (init_type==1) { // initialization with leading eigenvector

        // Compute the two anchor points
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end(); ite++) {
            int distance_val_tmp = ite->first;
            anchor_one_seq[ite->first] = NormalCdf( (distance_val_tmp+0.5)*min_space_unit, anchor_one*min_space_unit, sigma/sqrt(2)) - NormalCdf( (distance_val_tmp-0.5)*min_space_unit, anchor_one*min_space_unit, sigma/sqrt(2) );
        }
        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end(); ite++) {
            int distance_val_tmp = ite->first;
            anchor_two_seq[ite->first] = NormalCdf( (distance_val_tmp+0.5)*min_space_unit, anchor_two*min_space_unit, sigma/sqrt(2)) - NormalCdf( (distance_val_tmp-0.5)*min_space_unit, anchor_two*min_space_unit, sigma/sqrt(2) );
        }
            
        // use the probability vector as the initializer
        int row_idx, col_idx;
        
        for (int i=0; i<all_distance.size(); i++) {
        
            VectorXd smp_pos_init_tmp = VectorXd::Zero(M);
            smp_pos_init_tmp.segment(0, M-all_distance[i]).array() += valid_idx_pos.segment(0, M-all_distance[i]).array() * valid_idx_pos.segment(all_distance[i], M-all_distance[i]).array();
            D_mat_norm[all_distance[i]] = sqrt(smp_pos_init_tmp.sum());

            
            smp_pos_init_tmp *= all_distribution[all_distance[i]] / smp_pos_init_tmp.sum();
            smp_pos_init += smp_pos_init_tmp;
        }
        
        // Put the two anchor points
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end(); ite++) {
            smp_pos_init[ite->first] = anchor_one_seq[ite->first];
        }
        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end(); ite++) {
            smp_pos_init[ite->first] = anchor_two_seq[ite->first];
        }
        
        // Sum up the points other than the two anchor points and then normalize
        double smp_pos_init_exclude_sum = smp_pos_init.segment((anchor_one+round(tau/min_space_unit)+1), M-(4*round(tau/min_space_unit)+2)).sum();
        smp_pos_init.segment((anchor_one+round(tau/min_space_unit)+1), M-(4*round(tau/min_space_unit)+2)) = smp_pos_init.segment((anchor_one+round(tau/min_space_unit)+1), M-(4*round(tau/min_space_unit)+2)) / smp_pos_init_exclude_sum * (num_smp-2);

        VectorXd smp_pos_init_reverse = smp_pos_init.reverse();
        smp_pos_init = 0.5 * (smp_pos_init + smp_pos_init_reverse);
        
        obj_val = ComputeObjFun(smp_pos_init);
        cout<<"Obj_val: "<<obj_val<<endl;
        
        // use power method to compute the singular vector
        for (int ite=0; ite<max_sg_ite; ite++) {
            VectorXd smp_pos_init_pre = smp_pos_init;
            VectorXd smp_pos_init_tmp = VectorXd::Zero(M);
            for (int i=0; i<all_distance.size(); i++) {
                smp_pos_init_tmp.segment(0, M-all_distance[i]) += all_distribution[all_distance[i]] / pow(D_mat_norm[all_distance[i]], 2) * smp_pos_init_pre.segment(all_distance[i], M-all_distance[i]);
                smp_pos_init_tmp.segment(all_distance[i], M-all_distance[i]) += all_distribution[all_distance[i]] / pow(D_mat_norm[all_distance[i]], 2) * smp_pos_init_pre.segment(0, M-all_distance[i]);
            }

            smp_pos_init = VectorXd::Zero(M);
            
            for (int i=0; i<valid_idx_vec.size(); i++) {
                smp_pos_init(valid_idx_vec[i]) = smp_pos_init_tmp(valid_idx_vec[i]);
            }

            double smp_pos_init_norm = smp_pos_init.norm();

            smp_pos_init = smp_pos_init/smp_pos_init_norm;

            double sg_cvg_val = (smp_pos_init-smp_pos_init_pre).norm();
            
            cout<<ite<<" "<<sg_cvg_val<<endl;
            
            if (sg_cvg_val<sg_tol) {
                cout<<"Eig_iteration: "<<ite<<endl;
                break;
            }
        }

        smp_pos_init = smp_pos_init * sqrt(num_smp*1.0);

        // break even
        unsigned seed = std::chrono::steady_clock::now().time_since_epoch().count();
        default_random_engine generator(seed);
        normal_distribution<double> distribution(0, perturb_std);
        for (int i=0; i<M; i++) {
            smp_pos_init(i) = smp_pos_init(i) * (1+distribution(generator));
        }

        // map the initialization onto the convex set
        // Put the two anchor points
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end();     ite++) {
            smp_pos_init[ite->first] = 0;
        }
        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end();     ite++) {
            smp_pos_init[ite->first] = 0;
        }
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end();     ite++) {
            smp_pos_init[ite->first] += anchor_one_seq[ite->first];
        }
        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end();     ite++) {
            smp_pos_init[ite->first] += anchor_two_seq[ite->first];
        }

        // Project on to the l1-ball with box constraints
        VectorXd smp_pos_seg_tmp = VectorXd::Zero(valid_idx_vec_exclude.size());
        for (int i=0; i<valid_idx_vec_exclude.size(); i++) {
            smp_pos_seg_tmp(i) = smp_pos_init(valid_idx_vec_exclude[i]);
        }   

        smp_pos_seg_tmp = ProjectOntoCvxSet(smp_pos_seg_tmp, num_smp-2);
        for (int i=0; i<valid_idx_vec_exclude.size(); i++) {
            smp_pos_init(valid_idx_vec_exclude[i]) = smp_pos_seg_tmp(i);
        }   

        obj_val = ComputeObjFun(smp_pos_init);
        cout<<"Obj val: "<<obj_val<<endl;
        
    } else if (init_type==2) {  // initialize with random vector
    
        unsigned seed = std::chrono::steady_clock::now().time_since_epoch().count();
        default_random_engine generator(seed);
        normal_distribution<double> distribution(0, perturb_std);
        for (int i=0; i<M; i++) {
            smp_pos_init(i) = abs(distribution(generator));
        }

        // Compute the two anchor points
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end(); ite++) {
            int distance_val_tmp = ite->first;
            if ( ( distance_val_tmp >= (M-round(3*sigma/min_space_unit)) ) && ( distance_val_tmp <= M-1 ) ) {
                anchor_one_seq[ite->first] += NormalCdf( (distance_val_tmp+0.5)*min_space_unit, M*min_space_unit, sigma/sqrt(2)) - NormalCdf( (distance_val_tmp-0.5)*min_space_unit, M*min_space_unit, sigma/sqrt(2) );
            } else {
                anchor_one_seq[ite->first] += NormalCdf( (distance_val_tmp+0.5)*min_space_unit, anchor_one*min_space_unit, sigma/sqrt(2)) - NormalCdf( (distance_val_tmp-0.5)*min_space_unit, anchor_one*min_space_unit, sigma/sqrt(2) );
            }
        }

        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end(); ite++) {
            int distance_val_tmp = ite->first;
            if ( (distance_val_tmp >= 0) && (distance_val_tmp<=round(3*sigma/min_space_unit)) ) {
                anchor_two_seq[ite->first] += NormalCdf( (M+distance_val_tmp+0.5)*min_space_unit, anchor_two*min_space_unit, sigma/sqrt(2)) - NormalCdf( (M+distance_val_tmp-0.5)*min_space_unit, anchor_two*min_space_unit, sigma/sqrt(2) );
            } else {
                anchor_two_seq[ite->first] += NormalCdf( (distance_val_tmp+0.5)*min_space_unit, anchor_two*min_space_unit, sigma/sqrt(2)) - NormalCdf( (distance_val_tmp-0.5)*min_space_unit, anchor_two*min_space_unit, sigma/sqrt(2) );
            }
        }

        // Put the two anchor points
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end(); ite++) {
            smp_pos_init[ite->first] = 0;
        }
        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end(); ite++) {
            smp_pos_init[ite->first] = 0;
        }
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end();ite++) {
            smp_pos_init[ite->first] += anchor_one_seq[ite->first];
        }
        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end();ite++) {
            smp_pos_init[ite->first] += anchor_two_seq[ite->first];
        }


        // map the initialization onto the convex set
        // Put the two anchor points
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end();     ite++) {
            smp_pos_init[ite->first] = 0;
        }
        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end();     ite++) {
            smp_pos_init[ite->first] = 0;
        }
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end();     ite++) {
            smp_pos_init[ite->first] += anchor_one_seq[ite->first];
        }
        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end();     ite++) {
            smp_pos_init[ite->first] += anchor_two_seq[ite->first];
        }

        // Project on to the l1-ball with box constraints
        VectorXd smp_pos_seg_tmp = VectorXd::Zero(valid_idx_vec_exclude.size());
        for (int i=0; i<valid_idx_vec_exclude.size(); i++) {
            smp_pos_seg_tmp(i) = smp_pos_init(valid_idx_vec_exclude[i]);
        }

        smp_pos_seg_tmp = ProjectOntoCvxSet(smp_pos_seg_tmp, num_smp-2);
        for (int i=0; i<valid_idx_vec_exclude.size(); i++) {
            smp_pos_init(valid_idx_vec_exclude[i]) = smp_pos_seg_tmp(i);
        }

        //obj_val = ComputeObjFun(smp_pos_init);
        obj_val = 0;
        cout<<"Obj val: "<<obj_val<<endl;
    
    } else if (init_type==3) {  // initialize with uniform vector

        smp_pos_init = VectorXd::Ones(M);

        // Compute the two anchor points
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end(); ite++) {
            int distance_val_tmp = ite->first;
            if ( ( distance_val_tmp >= (M-round(3*sigma/min_space_unit)) ) && ( distance_val_tmp <= M-1 ) ) {
                anchor_one_seq[ite->first] += NormalCdf( (distance_val_tmp+0.5)*min_space_unit, M*min_space_unit, sigma/sqrt(2)) - NormalCdf( (distance_val_tmp-0.5)*min_space_unit, M*min_space_unit, sigma/sqrt(2) );
            } else {
                anchor_one_seq[ite->first] += NormalCdf( (distance_val_tmp+0.5)*min_space_unit, anchor_one*min_space_unit, sigma/sqrt(2)) - NormalCdf( (distance_val_tmp-0.5)*min_space_unit, anchor_one*min_space_unit, sigma/sqrt(2) );
            }
        }

        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end(); ite++) {
            int distance_val_tmp = ite->first;
            if ( (distance_val_tmp >= 0) && (distance_val_tmp<=round(3*sigma/min_space_unit)) ) {
                anchor_two_seq[ite->first] += NormalCdf( (M+distance_val_tmp+0.5)*min_space_unit, anchor_two*min_space_unit, sigma/sqrt(2)) - NormalCdf( (M+distance_val_tmp-0.5)*min_space_unit, anchor_two*min_space_unit, sigma/sqrt(2) );
            } else {
                anchor_two_seq[ite->first] += NormalCdf( (distance_val_tmp+0.5)*min_space_unit, anchor_two*min_space_unit, sigma/sqrt(2)) - NormalCdf( (distance_val_tmp-0.5)*min_space_unit, anchor_two*min_space_unit, sigma/sqrt(2) );
            }
        }

        // Put the two anchor points
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end(); ite++) {
            smp_pos_init[ite->first] = 0;
        }
        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end(); ite++) {
            smp_pos_init[ite->first] = 0;
        }
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end();ite++) {
            smp_pos_init[ite->first] += anchor_one_seq[ite->first];
        }
        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end();ite++) {
            smp_pos_init[ite->first] += anchor_two_seq[ite->first];
        }

        // break even
        unsigned seed = std::chrono::steady_clock::now().time_since_epoch().count();
        default_random_engine generator(seed);

        normal_distribution<double> distribution(0, perturb_std);
        for (int i=0; i<M; i++) {
            smp_pos_init(i) = smp_pos_init(i) * (1+distribution(generator));
        }

        // map the initialization onto the convex set
        // Put the two anchor points
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end();     ite++) {
            smp_pos_init[ite->first] = 0;
        }
        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end();     ite++) {
            smp_pos_init[ite->first] = 0;
        }
        for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end();     ite++) {
            smp_pos_init[ite->first] += anchor_one_seq[ite->first];
        }
        for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end();     ite++) {
            smp_pos_init[ite->first] += anchor_two_seq[ite->first];
        }

        // Project on to the l1-ball with box constraints
        VectorXd smp_pos_seg_tmp = VectorXd::Zero(valid_idx_vec_exclude.size());
        for (int i=0; i<valid_idx_vec_exclude.size(); i++) {
            smp_pos_seg_tmp(i) = smp_pos_init(valid_idx_vec_exclude[i]);
        }

        smp_pos_seg_tmp = ProjectOntoCvxSet(smp_pos_seg_tmp, num_smp-2);
        for (int i=0; i<valid_idx_vec_exclude.size(); i++) {
            smp_pos_init(valid_idx_vec_exclude[i]) = smp_pos_seg_tmp(i);
        }

        obj_val = ComputeObjFun(smp_pos_init);
        cout<<"Obj val: "<<obj_val<<endl;

    } else {
        cout<<"Unknown initialization type."<<endl;
        abort();
    }

    cout<<"Initialization finished."<<endl;

}

VectorXd uDGP::ProjectOntoCvxSet(VectorXd smp_vec, int num_smp_proj) {
    
    // make sure all the entries of v are above the convex set
    // Note that the vector index starts from 0
    double offset = -smp_vec.array().minCoeff() + num_smp_proj;
    smp_vec = smp_vec.array() + offset;
    
    VectorXd smp_sort_vec = smp_vec;
    sort(smp_sort_vec.data(), smp_sort_vec.data()+smp_sort_vec.size(), greater<double>());
    int smp_vec_len=smp_sort_vec.size();
    
    double theta = 0;
    int check_status = 0;
    for (int r=1; r<=num_smp_proj; r++) {
        VectorXd smp_sort_vec_new = smp_sort_vec.segment(r-1, smp_vec_len-r+1);
        VectorXd smp_sort_vec_new_cumsum = smp_sort_vec_new;
        for (int j=1; j<smp_sort_vec_new_cumsum.size(); j++) {
            smp_sort_vec_new_cumsum(j) += smp_sort_vec_new_cumsum(j-1);
        }
        VectorXd smp_sort_vec_new_thr = smp_sort_vec_new_cumsum;
        for (int j=0; j<smp_sort_vec_new_thr.size(); j++) {
            smp_sort_vec_new_thr(j) = smp_sort_vec_new(j) - (smp_sort_vec_new_thr(j)-(num_smp_proj-r+1))/(j+1);
        }
        int rho_new = -1;
        for (int j=smp_sort_vec_new_thr.size()-1; j>=0; j--) {
            if (smp_sort_vec_new_thr(j)>0) {
                rho_new = j+1;  // Note that the vector index starts from 0, we need to add 1 here
                break;
            }
        }
        if (rho_new==-1) {break;}
        
        // check that rho = rho_new+r-1 is larger than num_smp_proj
        if (rho_new+r-1<=num_smp_proj) {continue;}
        // Compute the threshold
        double theta_new = (smp_sort_vec_new_cumsum(rho_new-1)-(num_smp_proj-r+1))/rho_new;
        
        int break_marker = 0;
        double w_r = smp_sort_vec(r-1)-theta_new;
        if ( (w_r>0) && (w_r<1) ) {
            if (r==1) {
                break_marker = 1;
                check_status = 1;
            } else {
                double w_rm1 = smp_sort_vec(r-2)-theta_new ;
                if (w_rm1>=1) {
                    break_marker=1;
                    check_status=1;
                }
            }
        } else {
            continue;
        }
        
        theta = theta_new;
        if (break_marker==1) {
            break;
        }
    }
    
    VectorXd smp_vec_proj = smp_vec;
    
    if (check_status==1) {
        smp_vec_proj = smp_vec.array() - theta;
        for (int i=0; i<smp_vec_len; i++) {
            if (smp_vec_proj(i)<0) {
                smp_vec_proj(i)=0;
            }
            if (smp_vec_proj(i)>1) {
                smp_vec_proj(i)=1;
            }
        }
    } else {
        // set the top N entries to 1 and the rest to 0
        double thd_tmp = smp_sort_vec(num_smp_proj-1);
        for (int i=0; i<smp_vec_len; i++) {
            if (smp_vec_proj(i)>=thd_tmp) {
                smp_vec_proj(i)=1;
            } else {
                smp_vec_proj(i)=0;
            }
        }
    }
    
    return smp_vec_proj;
    
}


void uDGP::GradientDescent() {

    smp_pos = smp_pos_init;

    step = step_ori;
    obj_val = ComputeObjFun(smp_pos);

    VectorXd smp_pos_pre;
    double obj_val_pre;
    int ite;
   
    int num_cov = 0;
    for (ite=1; ite<max_ite; ite++) {

        //for (int i=0; i<all_distance.size(); i++) {
        //    if (all_distribution[all_distance[i]]>0) {
        //        cout<<all_distance[i]<<" "<<all_distribution[all_distance[i]]<<" "<<est_distribution[all_distance[i]]<<" ";
        //    }
        //}
        //cout<<endl;
        
        smp_pos_pre = smp_pos;
        obj_val_pre = obj_val;
        
        VectorXd smp_pos_der = ComputeGradient(smp_pos_pre);
        
        smp_pos_der = smp_pos_der.array() - smp_pos_der.sum()/M;
        
        
        VectorXd smp_pos_tmp = VectorXd::Zero(M);
        
        while (step>step_thd) {
            smp_pos_tmp = smp_pos_pre-smp_pos_der*step;
            
            VectorXd smp_pos_seg_tmp = VectorXd::Zero(valid_idx_vec_exclude.size());
            for (int i=0; i<valid_idx_vec_exclude.size(); i++) {
                smp_pos_seg_tmp(i) = smp_pos_tmp(valid_idx_vec_exclude[i]);
            }
            smp_pos_tmp = VectorXd::Zero(M);

            smp_pos_seg_tmp = ProjectOntoCvxSet(smp_pos_seg_tmp, num_smp-2);
            for (int i=0; i<valid_idx_vec_exclude.size(); i++) {
                smp_pos_tmp(valid_idx_vec_exclude[i]) = smp_pos_seg_tmp(i);
            }
            
            // Put the two anchor points
            for (map<int, double>::iterator ite=anchor_one_seq.begin(); ite!=anchor_one_seq.end(); ite++) {
                smp_pos_tmp[ite->first] = anchor_one_seq[ite->first];
            }
            for (map<int, double>::iterator ite=anchor_two_seq.begin(); ite!=anchor_two_seq.end(); ite++) {
                smp_pos_tmp[ite->first] = anchor_two_seq[ite->first];
            }
            
            obj_val = ComputeObjFun(smp_pos_tmp);
            if ((obj_val<obj_val_pre)||(obj_val==0)) {
                step = step/bkt_rate;
                break;
            } else {
                step = step*bkt_rate;
            }
        }
        
        if (step<=step_thd) {
            cout<<"Step size too small!" << "\t"<<step<<"\t"<<smp_pos_tmp.sum()<<"\t"<<num_smp<<endl;
            break;
        }
        
        smp_pos = smp_pos_tmp;
        
        VectorXd smp_pos_diff = smp_pos - smp_pos_pre;
        double cvg_val = smp_pos_diff.norm() / smp_pos.norm();
        
        if (cvg_val<cvg_thd) {
            num_cov += 1;
        }

        if (num_cov>=10) {
            cout<<"Convergence reached!"<<endl;
            break;
        }

        // Save the results at each iteration
        // choose between the trunc and app mode when saving the results
        //ofstream write_result(output_ite_file, ios_base::trunc);
        //ofstream write_result(output_ite_file, ios_base::app);

        //write_result<<obj_val<<" ";
        //for (int i=0; i<smp_pos.size(); i++) {
        //    write_result<<smp_pos(i)<<" ";
        //}
        //write_result<<"\n";
        //write_result.close();

        cout<<"Ite: "<<ite<<"\t"<<step<<"\t"<<obj_val<<"\t"<<cvg_val<<"\t"<<smp_pos.sum()<<endl;   
    }
    
    if (ite>=max_ite) {
        cout<<"Max iteration reached!"<<endl;
    }
    
}

VectorXd uDGP::ComputeGradient(VectorXd smp_pos_vect) {return VectorXd::Zero(1);}
void uDGP::ComputeGradientMuti( vector<int> all_distance_block, VectorXd* smp_vec_muti, VectorXd* smp_der_seq_pt, int val_idx ) {}
double uDGP::ComputeObjFun(VectorXd smp_pos_vect) { return 0.0;}
void uDGP::ComputeObjFunMuti(vector<int> all_distance_block, VectorXd* smp_vec_muti, vector<double>* obj_seq_pt, int val_idx ) {}

VectorXd uDGP::GetSamplePos() {return smp_pos;}
double uDGP::GetObjFun() {return obj_val;}
uDGP::~uDGP() {}
