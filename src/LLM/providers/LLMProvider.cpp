#include "LLM/providers/LLMProvider.hpp"

namespace LLM
{

Provider::Provider(QObject *parent) : QObject(parent) {}

Provider::~Provider() = default;

} // namespace LLM
