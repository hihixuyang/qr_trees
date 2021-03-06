//
// Underlying iLQRNode for Tree-iLQR.
//
// Arun Venkatraman (arunvenk@cs.cmu.edu)
// December 2016
//

#pragma once

#include <ilqr/ilqr_taylor_expansions.hh>

#include <Eigen/Dense>

#include <functional>
#include <memory>
#include <ostream>
#include <vector>

namespace ilqr 
{

// Each plan node represents a timestep.
class iLQRNode
{
public:
    iLQRNode(const int state_dim, 
             const int control_dim, 
             const DynamicsFunc &dynamics_func, 
             const CostFunc &cost_func,
             const double probablity);

    iLQRNode(const Eigen::VectorXd &x_star,
             const Eigen::VectorXd &u_star,
             const DynamicsFunc &dynamics_func, 
             const CostFunc &cost_func,
             const double probablity);

    // Compute the control policy and quadratic value of the node given the next
    // timestep value. The policy and value are set directly in the node.
    //void bellman_backup(const QuadraticValue& Jt1);
    void bellman_backup(const std::vector<std::shared_ptr<iLQRNode>> &children);

    // Computes a feedback control from state xt.
    Eigen::VectorXd compute_control(const Eigen::VectorXd &xt) const;
    // Computes a feedback control from state xt except moving only "alpha" step-size away from the
    // expansion point u().
    Eigen::VectorXd compute_control(const Eigen::VectorXd &xt, const double alpha) const;

    // Get and set the probability
    double probability() const { return probability_; }
    void set_probability(double p) { probability_ = p; }

    DynamicsFunc& dynamics_func() { return dynamics_func_; }
    CostFunc& cost_func() { return cost_func_; }
    const DynamicsFunc& dynamics_func() const { return dynamics_func_; }
    const CostFunc& cost_func() const { return cost_func_; }

    // Current Taylor expansion points x and u.
    Eigen::VectorXd& x() { return x_; }
    Eigen::VectorXd& u() { return u_; }
    const Eigen::VectorXd& x() const { return x_; }
    const Eigen::VectorXd& u() const { return u_; }

    // Original Taylor expansion points x and u.
    Eigen::VectorXd& orig_xstar() { return orig_xstar_; }
    Eigen::VectorXd& orig_ustar() { return orig_ustar_; }
    const Eigen::VectorXd& orig_xstar() const { return orig_xstar_; }
    const Eigen::VectorXd& orig_ustar() const { return orig_ustar_; }

    const QuadraticValue& value() const { return J_; };
    QuadraticValue& value() { return J_; };

    const Eigen::MatrixXd& K() const { return K_; };
    Eigen::MatrixXd& K() { return K_; };
    
    const Eigen::VectorXd& k() const { return k_; };
    Eigen::VectorXd& k() { return k_; };

private:
    DynamicsFunc dynamics_func_;
    CostFunc cost_func_;

    // Set point used for linearization of THIS node's cost and
    // all CHILD node dynamics.
    Eigen::VectorXd x_;
    Eigen::VectorXd u_;

    // Probability of transitioning to this node from the parent.
    double probability_;

    // The terms of the quadratic value function, 1/2 * x^T V x + Gx + W.
    QuadraticValue J_; 

    // Feedback gain matrix on the extended-state, [dim(u)] x [dim(x) + 1]
    Eigen::MatrixXd K_; 
    // Feed-forward control matrix, [dim(u)] x [dim(1)].
    Eigen::VectorXd k_; 

    // Original nominal state specified at the beginning of iLQR. [dim(x)] x [1]
    Eigen::VectorXd orig_xstar_; 
    // Original nominal control specified at the beginning of iLQR. [dim(u)] x [1]
    Eigen::VectorXd orig_ustar_; 
};

// Allows the iLQRNode to be printed.
std::ostream& operator<<(std::ostream& os, const ilqr::iLQRNode& node);

} // namespace ilqr

