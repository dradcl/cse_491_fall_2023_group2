#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <execution>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace cowboys {
  class GraphNode;
  using NodeFunction = double (*)(const GraphNode &);

  /// @brief A node in a decision graph.
  /// @note This should always be a shared pointer. Caching will not work otherwise.
  class GraphNode : public std::enable_shared_from_this<GraphNode> {
  protected:
    /// The input nodes to this node.
    std::vector<std::shared_ptr<GraphNode>> inputs;

    /// The function that operates on the outputs from a node's input nodes.
    NodeFunction function_pointer{nullptr};

    /// The default output of this node.
    double default_output{0};

    /// The nodes connected to this node's output.
    std::vector<GraphNode *> outputs;

    /// The cached output of this node.
    mutable double cached_output{0};

    /// Flag indicating whether the cached output is valid.
    mutable bool cached_output_valid{false};

    /// @brief Add an output node to this node. Used for cache invalidation.
    /// @param node The node to add as an output.
    void AddOutput(GraphNode *node) { outputs.push_back(node); }

    /// @brief Invalidates this node's cache and the caches of all nodes that depend on this node.
    void RecursiveInvalidateCache() const {
      cached_output_valid = false;
      for (auto &output : outputs) {
        output->RecursiveInvalidateCache();
      }
    }

  public:
    GraphNode() = default;
    ~GraphNode() = default;

    /// TODO: Check guidelines for this
    GraphNode(double default_value) : default_output{default_value} {}
    GraphNode(NodeFunction function) : function_pointer{function} {}

    /// @brief Get the output of this node. Performs caching.
    /// @return The output of this node.
    double GetOutput() const {
      if (cached_output_valid)
        return cached_output;

      double result = default_output;
      // Invoke function pointer if it exists
      if (function_pointer != nullptr) {
        result = function_pointer(*this);
      }

      // Cache the output
      cached_output = result;
      cached_output_valid = true;

      return result;
    }

    /// @brief Get the output values of the inputs of this node.
    /// @return A vector of doubles representing the input values.
    std::vector<double> GetInputValues() const {
      std::vector<double> values;
      values.reserve(inputs.size());
      std::transform(inputs.cbegin(), inputs.cend(), std::back_inserter(values),
                     [](const auto &node) { return node->GetOutput(); });
      return values;
    }

    /// @brief Get the output values of the inputs of this node given an array of indices.
    /// @tparam N The size of the indices array.
    /// @param indices The indices of the inputs to get the output values of.
    /// @return A vector of doubles representing the input values in the same order of the indices.
    template <size_t N> std::optional<std::vector<double>> GetInputValues(const std::array<size_t, N> &indices) const {
      size_t max_index = *std::max_element(indices.cbegin(), indices.cend());
      if (max_index >= inputs.size())
        return std::nullopt;
      std::vector<double> values;
      values.reserve(N);
      std::transform(indices.cbegin(), indices.cend(), std::back_inserter(values),
                     [this](const auto &index) { return inputs.at(index)->GetOutput(); });
      return values;
    }

    /// @brief Set the function pointer of this node.
    /// @param function The function for this node to use.
    void SetFunctionPointer(NodeFunction function) {
      function_pointer = function;
      RecursiveInvalidateCache();
    }

    /// @brief Add an input node to this node.
    /// @param node The node to add as an input.
    void AddInput(std::shared_ptr<GraphNode> node) {
      inputs.push_back(node);
      // Add a weak pointer to this node to the input node's outputs
      node->AddOutput(this);
      RecursiveInvalidateCache();
    }

    /// @brief Append nodes in a vector to this node's list of inputs.
    /// @param nodes The nodes to add as inputs.
    void AddInputs(const std::vector<std::shared_ptr<GraphNode>> &nodes) {
      inputs.insert(inputs.cend(), nodes.cbegin(), nodes.cend());
      RecursiveInvalidateCache();
    }

    /// @brief Set the input nodes of this node.
    /// @param nodes
    void SetInputs(std::vector<std::shared_ptr<GraphNode>> nodes) {
      inputs = nodes;
      RecursiveInvalidateCache();
    }

    /// @brief Set the default output of this node.
    /// @param value The new default output.
    void SetDefaultOutput(double value) {
      if (default_output != value) {
        default_output = value;
        RecursiveInvalidateCache();
      }
    }

    /// @brief Get the default output of this node.
    /// @return The default output.
    double GetDefaultOutput() const { return default_output; }

    /// @brief Check if the cached output is valid.
    /// @return True if the cached output is valid, false otherwise.
    bool IsCacheValid() const { return cached_output_valid; }
  };

  /// @brief Returns the sum all inputs.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double Sum(const GraphNode &node) {
    auto vals = node.GetInputValues();
    return std::reduce(std::execution::par, vals.cbegin(), vals.cend(), 0.);
  }

  /// @brief Returns 1 if all inputs are not equal to 0, 0 otherwise.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double And(const GraphNode &node) {
    auto vals = node.GetInputValues();
    return std::any_of(vals.cbegin(), vals.cend(), [](const double val) { return val == 0.; }) ? 0. : 1.;
  }

  /// @brief Returns 1 if any of the inputs besides the first are equal to the first
  /// input, 0 otherwise.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double AnyEq(const GraphNode &node) {
    std::vector<double> vals = node.GetInputValues();
    if (vals.size() == 0)
      return node.GetDefaultOutput();
    for (size_t i = 1; i < vals.size(); ++i) {
      if (vals.at(0) == vals.at(i))
        return 1.;
    }
    return 0.;
  }

  /// @brief Returns 1 if the first input is equal to 0 or there are no inputs, 0 otherwise.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double Not(const GraphNode &node) {
    auto vals = node.GetInputValues<1>(std::array<size_t, 1>{0});
    if (!vals.has_value())
      return node.GetDefaultOutput();
    return (*vals)[0] == 0. ? 1. : 0.;
  }

  /// @brief Returns the input with index 0 if the condition (input with index
  /// 1) is not 0.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double Gate(const GraphNode &node) {
    auto vals = node.GetInputValues<2>(std::array<size_t, 2>{0, 1});
    if (!vals.has_value())
      return node.GetDefaultOutput();
    return (*vals)[1] != 0. ? (*vals)[0] : 0.;
  }

  /// @brief Sums the sin(x) of all inputs.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double Sin(const GraphNode &node) {
    std::vector<double> vals = node.GetInputValues();
    return std::transform_reduce(std::execution::par, vals.cbegin(), vals.cend(), 0., std::plus{},
                                 [](const double val) { return std::sin(val); });
  }

  /// @brief Sums the cos(x) of all inputs.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double Cos(const GraphNode &node) {
    std::vector<double> vals = node.GetInputValues();
    return std::transform_reduce(std::execution::par, vals.cbegin(), vals.cend(), 0., std::plus{},
                                 [](const double val) { return std::cos(val); });
  }

  /// @brief Returns the product of all inputs.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double Product(const GraphNode &node) {
    auto vals = node.GetInputValues();
    return std::reduce(std::execution::par, vals.cbegin(), vals.cend(), 1., std::multiplies{});
  }

  /// @brief Returns the sum of the exp(x) of all inputs.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double Exp(const GraphNode &node) {
    std::vector<double> vals = node.GetInputValues();
    return std::transform_reduce(std::execution::par, vals.cbegin(), vals.cend(), 0., std::plus{},
                                 [](const double val) { return std::exp(val); });
  }

  /// @brief Returns 1 if all inputs are in ascending, 0 otherwise. If only one input, then defaults to 1.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double LessThan(const GraphNode &node) {
    std::vector<double> vals = node.GetInputValues();
    return std::ranges::is_sorted(vals, std::less{});
  }

  /// @brief Returns 1 if all inputs are in ascending, 0 otherwise. If only one input, then defaults to 1.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double GreaterThan(const GraphNode &node) {
    std::vector<double> vals = node.GetInputValues();
    return std::ranges::is_sorted(vals, std::greater{});
  }

  /// @brief Returns the maximum value of all inputs.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double Max(const GraphNode &node) {
    std::vector<double> vals = node.GetInputValues();
    if (vals.empty())
      return node.GetDefaultOutput();
    return *std::max_element(vals.cbegin(), vals.cend());
  }

  /// @brief Returns the minimum value of all inputs.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double Min(const GraphNode &node) {
    std::vector<double> vals = node.GetInputValues();
    if (vals.empty())
      return node.GetDefaultOutput();
    return *std::min_element(vals.cbegin(), vals.cend());
  }

  /// @brief Returns the sum of negated inputs.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double NegSum(const GraphNode &node) { return -Sum(node); }

  /// @brief Returns the sum of squared inputs.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double Square(const GraphNode &node) {
    std::vector<double> vals = node.GetInputValues();
    return std::transform_reduce(std::execution::par, vals.cbegin(), vals.cend(), 0., std::plus{},
                                 [](const double val) { return val * val; });
  }

  /// @brief Returns the sum of positively clamped inputs.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double PosClamp(const GraphNode &node) {
    std::vector<double> vals = node.GetInputValues();
    return std::transform_reduce(std::execution::par, vals.cbegin(), vals.cend(), 0., std::plus{},
                                 [](const double val) { return std::max(0., val); });
  }

  /// @brief Returns the sum of negatively clamped inputs.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double NegClamp(const GraphNode &node) {
    std::vector<double> vals = node.GetInputValues();
    return std::transform_reduce(std::execution::par, vals.cbegin(), vals.cend(), 0., std::plus{},
                                 [](const double val) { return std::min(0., val); });
  }

  /// @brief Returns the sum of square root of positively clamped inputs.
  /// @param node The node to get the inputs from.
  /// @return The function result as a double.
  double Sqrt(const GraphNode &node) {
    std::vector<double> vals = node.GetInputValues();
    return std::transform_reduce(std::execution::par, vals.cbegin(), vals.cend(), 0., std::plus{},
                                 [](const double val) { return std::sqrt(std::max(0., val)); });
  }

  /// @brief A vector of all the node functions.
  const std::vector<NodeFunction> FUNCTION_SET{nullptr, Sum,     And,      AnyEq,    Not,         Gate, Sin,
                                               Cos,     Product, Exp,      LessThan, GreaterThan, Max,  Min,
                                               NegSum,  Square,  PosClamp, NegClamp, Sqrt};
} // namespace cowboys