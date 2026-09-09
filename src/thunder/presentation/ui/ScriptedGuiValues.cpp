#include "thunder/presentation/ui/ScriptedGuiRuntime.hpp"

namespace thunder {

UiDataValue UiDataValue::boolean_value(bool value) noexcept {
    UiDataValue val{};
    val.type = UiValueType::Boolean;
    val.boolean = value;
    return val;
}

UiDataValue UiDataValue::number_value(double value) noexcept {
    UiDataValue val{};
    val.type = UiValueType::Number;
    val.number = value;
    return val;
}

UiDataValue UiDataValue::text_value(std::string_view value) noexcept {
    UiDataValue val{};
    val.type = UiValueType::Text;
    val.text = value;
    return val;
}

UiDataValue UiDataValue::key_value(UiValueType type, UiStableKey value) noexcept {
    UiDataValue val{};
    val.type = type;
    val.stable_key = value;
    return val;
}

UiDataValue UiDataValue::entity_value(UiDataEntityRef value) noexcept {
    UiDataValue val{};
    val.type = UiValueType::Entity;
    val.entity = value;
    return val;
}

UiDataValue UiDataValue::collection_value(UiDataCollectionRef value) noexcept {
    UiDataValue val{};
    val.type = UiValueType::Collection;
    val.collection = value;
    return val;
}

UiDataValue UiDataValue::series_value(UiValueType type, UiDataSeriesRef value) noexcept {
    UiDataValue val{};
    val.type = type;
    val.series = value;
    return val;
}
} // namespace thunder
