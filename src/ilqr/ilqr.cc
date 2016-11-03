#include <ilqr/ilqr.hh>

#include <utils/debug_utils.hh>

#include <algorithm>
#include <iterator>
#include <unordered_map>

namespace
{
    // Double precision equality checking epsilon.
    constexpr double EPS = 1e-5;

} // namespace

iLQRTree::iLQRTree(int state_dim, int control_dim)
    : state_dim_(state_dim),
      control_dim_(control_dim),
      ZeroValueMatrix_(Eigen::MatrixXd::Zero(state_dim + 1, state_dim + 1))
{
}

std::shared_ptr<PlanNode> iLQRTree::make_plan_node(const Eigen::VectorXd &x_star, 
                                const Eigen::VectorXd &u_star, 
                                const DynamicsFunc &dynamics_func, 
                                const CostFunc &cost_func,
                                const double probability)
{
    IS_EQUAL(x_star.size(), state_dim_);
    IS_EQUAL(u_star.size(), control_dim_);

    std::shared_ptr<PlanNode> plan_node 
        = std::make_shared<PlanNode>(state_dim_, control_dim_, dynamics_func, cost_func, probability);

    // Add node creation, the nominal state/control and the forward pass state/control used for 
    // differentiating the Dynamics and cost function are the same.
    plan_node->set_xstar(x_star);
    plan_node->set_ustar(u_star);
    plan_node->set_x(x_star);
    plan_node->set_u(u_star);

    // Update the linearization and quadraticization of the dynamics and cost respectively.
    plan_node->update_dynamics();
    plan_node->update_cost();

    return plan_node;
}

TreeNodePtr iLQRTree::add_root(const Eigen::VectorXd &x_star, const Eigen::VectorXd &u_star, 
        const DynamicsFunc &dynamics_func, const CostFunc &cost_func)
{
    return add_root(make_plan_node(x_star, u_star, dynamics_func, cost_func, 1.0)); 
}

TreeNodePtr iLQRTree::add_root(const std::shared_ptr<PlanNode> &plan_node)
{
    tree_ = data::Tree<PlanNode>(plan_node);
    return tree_.root();
}

std::vector<TreeNodePtr> iLQRTree::add_nodes(const std::vector<std::shared_ptr<PlanNode>> &plan_nodes, 
        TreeNodePtr &parent)
{
    // Confirm the probabilities in the plan nodes sum to 1.
    const double probability_sum = 
        std::accumulate(plan_nodes.begin(), plan_nodes.end(), 0.0,
            [](const double a, const std::shared_ptr<PlanNode> &node) 
            {
                return a + node->probability_;
            }
            );
    IS_ALMOST_EQUAL(probability_sum, 1.0, EPS); // Throw error if sum is not close to 1.0

    // Create tree nodes from the plan nodes and add them to the tree.
    std::vector<TreeNodePtr> children;
    children.reserve(plan_nodes.size());
    for (const auto &plan_node : plan_nodes)
    {
        children.emplace_back(tree_.add_child(parent, plan_node));
    }
    return children;
}

TreeNodePtr iLQRTree::root()
{
    return tree_.root();
}

void iLQRTree::bellman_tree_backup()
{
   auto all_children = tree_.leaf_nodes();
}

std::list<TreeNodePtr> iLQRTree::backup_to_parents(const std::list<TreeNodePtr> &all_children)
{
   // Hash the leaves by their parent so we can process all the children for a parent.    
   std::unordered_map<TreeNodePtr, std::list<TreeNodePtr>> parent_map;

   // Start at the leaves and work up the tree. 
   for (auto &child : all_children)
   {
       parent_map[child->parent()].push_back(child);
   }

   std::list<TreeNodePtr> parents;
   for (auto &parent_children_pair : parent_map)
   {
       // Compute the Value matrix (V_t) of the parent by using each child's Value matrix (V_{t+1})
       // and weighting them by the probability.
       Eigen::MatrixXd Vt = Eigen::MatrixXd::Zero(state_dim_, state_dim_);
       std::shared_ptr<PlanNode> parent_plan_node = parent_children_pair.first->item();
       auto &children = parent_children_pair.second;
       for (auto &child : children)
       {
           const std::shared_ptr<PlanNode> &child_plan_node = child->item();
           const double p = child_plan_node->probability_; 
           const Eigen::MatrixXd Vt_temp = compute_value_matrix(parent_plan_node, child_plan_node->V_);
           Vt += p*Vt_temp;
       }
       parent_plan_node->V_ = Vt; 
       parents.push_back(parent_children_pair.first);
   }

   return parents;
}

Eigen::MatrixXd iLQRTree::compute_value_matrix(const std::shared_ptr<PlanNode> &node, 
                                               const Eigen::MatrixXd &Vt1)
{
    // Extract dynamics terms.
    const Eigen::MatrixXd &A = node->dynamics_.A;
    const Eigen::MatrixXd &B = node->dynamics_.B;
    // Extract cost terms.
    const Eigen::MatrixXd &Q = node->cost_.Q;
    const Eigen::MatrixXd &P = node->cost_.P;
    const Eigen::MatrixXd &b_u = node->cost_.b_u;
    // Extract control policy terms.
    const Eigen::MatrixXd &K = node->K_;
    const Eigen::MatrixXd &k = node->k_;

    const Eigen::MatrixXd cntrl_cross_term = P + A.transpose() * Vt1 * B;
    Eigen::MatrixXd quadratic_term = Q + A.transpose() * Vt1 * A + cntrl_cross_term*K;
    IS_EQUAL(quadratic_term.rows(), state_dim_ + 1);
    IS_EQUAL(quadratic_term.cols(), state_dim_ + 1);

    Eigen::MatrixXd linear_term = cntrl_cross_term*k;
    IS_EQUAL(quadratic_term.rows(), state_dim_ + 1);
    IS_EQUAL(quadratic_term.cols(), 1);

    Eigen::MatrixXd constant_term = b_u.transpose() * k;

    Eigen::MatrixXd Vt = quadratic_term; 
    Vt.topRightCorner(state_dim_, 1) += linear_term.topRows(state_dim_);
    Vt.bottomLeftCorner(1, state_dim_) += linear_term.topRows(state_dim_).transpose();
    Vt.bottomRightCorner(1, 1) += constant_term;

    return Vt;
}

void iLQRTree::compute_control_policy(std::shared_ptr<PlanNode> &node, const Eigen::MatrixXd &Vt1)
{
    const Eigen::MatrixXd &A = node->dynamics_.A;
    const Eigen::MatrixXd &B = node->dynamics_.B;
    // Extract cost terms.
    const Eigen::MatrixXd &P = node->cost_.P;
    const Eigen::MatrixXd &R = node->cost_.R;
    const Eigen::MatrixXd &b_u = node->cost_.b_u;

    const Eigen::MatrixXd inv_cntrl_term = (R + B.transpose()*Vt1*B).inverse();

    node->K_ = -1.0 * inv_cntrl_term * (P.transpose() + B.transpose() * Vt1 * A);
    node->k_ = -1.0 * inv_cntrl_term * b_u; 
}