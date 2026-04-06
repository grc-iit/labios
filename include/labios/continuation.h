#pragma once

#include <labios/channel.h>
#include <labios/label.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <optional>
#include <string>
#include <string_view>

namespace labios {

/// Process a label's continuation after successful completion.
/// Returns the chained label if one was created (Chain/Conditional).
/// For Notify, publishes to the channel and returns nullopt.
/// For None, returns nullopt immediately.
std::optional<LabelData> process_continuation(
    const LabelData& completed_label,
    const CompletionData& completion,
    ChannelRegistry& channels,
    transport::NatsConnection& nats,
    transport::RedisConnection& redis);

/// Encode a template LabelData as a chain_params string (hex-encoded serialized label).
std::string encode_chain_params(const LabelData& label_template);

/// Decode chain_params back into a LabelData.
LabelData decode_chain_params(std::string_view encoded);

/// Evaluate a condition expression against completion data.
/// Supports: field==value, field!=value, field>value, field<value
/// Fields: status, data_size, error, label_id
bool evaluate_condition(std::string_view condition,
                        const LabelData& completed_label,
                        const CompletionData& completion);

} // namespace labios
