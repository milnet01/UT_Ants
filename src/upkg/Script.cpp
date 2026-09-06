#include "upkg/Script.h"

#include "upkg/ByteReader.h"
#include "upkg/Package.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace uta::upkg {
namespace {

// The memory widths ScriptSize was computed against. Fixed constants, never
// sizeof: the compiler that wrote these packages in 1999 was 32-bit, so an
// object pointer and an FName are four bytes each whatever this host is.
// sizeof(void*) gives eight on every machine this project builds on, which
// overshoots ScriptSize and is refused rather than silently mis-walking.
// INV-4.
constexpr std::size_t POINTER_BYTES = 4;
constexpr std::size_t NAME_BYTES = 4;

/// How deep an expression may nest before the input is treated as hostile.
/// A cap rather than a stack overflow: INV-3's "never guesses" applies to
/// recursion too.
constexpr int MAX_EXPRESSION_DEPTH = 64;

// The instruction set, by the names UnrealScript's own compiler used. An
// opcode absent from this table is MalformedData -- the walker never guesses
// a length and never resynchronises by scanning (SS 4.4).
enum : std::uint8_t {
    EX_LocalVariable = 0x00,
    EX_InstanceVariable = 0x01,
    EX_DefaultVariable = 0x02,
    EX_Return = 0x04,
    EX_Switch = 0x05,
    EX_Jump = 0x06,
    EX_JumpIfNot = 0x07,
    EX_Stop = 0x08,
    EX_Assert = 0x09,
    EX_Case = 0x0A,
    EX_Nothing = 0x0B,
    EX_LabelTable = 0x0C,
    EX_GotoLabel = 0x0D,
    EX_EatString = 0x0E,
    EX_Let = 0x0F,
    EX_DynArrayElement = 0x10,
    EX_New = 0x11,
    EX_ClassContext = 0x12,
    EX_MetaCast = 0x13,
    EX_LetBool = 0x14,
    EX_EndFunctionParms = 0x16,
    EX_Self = 0x17,
    EX_Skip = 0x18,
    EX_Context = 0x19,
    EX_ArrayElement = 0x1A,
    EX_VirtualFunction = 0x1B,
    EX_FinalFunction = 0x1C,
    EX_IntConst = 0x1D,
    EX_FloatConst = 0x1E,
    EX_StringConst = 0x1F,
    EX_ObjectConst = 0x20,
    EX_NameConst = 0x21,
    EX_RotationConst = 0x22,
    EX_VectorConst = 0x23,
    EX_ByteConst = 0x24,
    EX_IntZero = 0x25,
    EX_IntOne = 0x26,
    EX_True = 0x27,
    EX_False = 0x28,
    EX_NativeParm = 0x29,
    EX_NoObject = 0x2A,
    EX_IntConstByte = 0x2C,
    EX_BoolVariable = 0x2D,
    EX_DynamicCast = 0x2E,
    EX_Iterator = 0x2F,
    EX_IteratorPop = 0x30,
    EX_IteratorNext = 0x31,
    EX_StructCmpEq = 0x32,
    EX_StructCmpNe = 0x33,
    EX_UnicodeStringConst = 0x34,
    EX_StructMember = 0x36,
    EX_GlobalFunction = 0x38,

    /// 0x39 to 0x5F are the primitive conversions -- one operand each.
    EX_FirstConversion = 0x39,
    EX_LastConversion = 0x5F,

    /// An extended native call: this byte and the next together form the
    /// native index.
    EX_FirstExtendedNative = 0x60,
    /// A native call, the index being the opcode itself.
    EX_FirstNative = 0x70,
};

Error malformed(std::string message) {
    return Error(ErrorCode::MalformedData, std::move(message));
}

/// An opcode as it is written in the format's own documentation, so a failure
/// message can be matched against the table above.
std::string hexByte(std::uint8_t value) {
    constexpr char DIGITS[] = "0123456789ABCDEF";
    return std::string{'0', 'x', DIGITS[(value >> 4) & 0x0F], DIGITS[value & 0x0F]};
}

/// One walked instruction: how many MEMORY bytes it costs, and which opcode
/// it was. The opcode comes back because a parameter list ends at the
/// instruction EX_EndFunctionParms rather than at a count.
struct Walked {
    std::size_t memoryBytes = 0;
    std::uint8_t opcode = 0;
};

[[nodiscard]] Result<Walked> skipExpression(const Package& package, ByteReader& reader,
                                            int depth);

/// Walk a call's parameters, which run to an EX_EndFunctionParms rather than
/// to a count. That terminator is itself an instruction and costs its byte.
[[nodiscard]] Result<std::size_t> skipParameters(const Package& package,
                                                 ByteReader& reader, int depth) {
    std::size_t total = 0;
    for (;;) {
        UTA_TRY(const Walked walked, skipExpression(package, reader, depth + 1));
        total += walked.memoryBytes;
        if (walked.opcode == EX_EndFunctionParms) {
            return total;
        }
    }
}

Result<Walked> skipExpression(const Package& package, ByteReader& reader, int depth) {
    if (depth > MAX_EXPRESSION_DEPTH) {
        return std::unexpected(malformed("a script expression nests deeper than " +
                                         std::to_string(MAX_EXPRESSION_DEPTH) +
                                         " levels"));
    }

    UTA_TRY(const std::uint8_t opcode, reader.readU8());
    Walked walked;
    walked.opcode = opcode;

    // A nested expression, one level down. Its own opcode is charged inside.
    const auto nested = [&](std::size_t& into) -> Result<void> {
        UTA_TRY(const Walked inner, skipExpression(package, reader, depth + 1));
        into += inner.memoryBytes;
        return {};
    };

    // Every instruction costs its opcode byte, plus the memory widths of its
    // operands. Leaving the opcode byte out is the easy mistake and nothing
    // else in the walk restores it: an instruction carrying one object
    // reference costs five, not four.
    walked.memoryBytes = 1;

    if (opcode >= EX_FirstNative) {
        UTA_TRY(const std::size_t parameters, skipParameters(package, reader, depth));
        walked.memoryBytes += parameters;
        return walked;
    }
    if (opcode >= EX_FirstExtendedNative) {
        UTA_TRY([[maybe_unused]] const std::uint8_t second, reader.readU8());
        walked.memoryBytes += 1;
        UTA_TRY(const std::size_t parameters, skipParameters(package, reader, depth));
        walked.memoryBytes += parameters;
        return walked;
    }

    switch (opcode) {
    // An object reference, written as a compact index and counted as a
    // pointer.
    case EX_LocalVariable:
    case EX_InstanceVariable:
    case EX_DefaultVariable:
    case EX_ObjectConst:
    case EX_NativeParm: {
        UTA_TRY([[maybe_unused]] const std::int32_t reference, reader.readIndex());
        walked.memoryBytes += POINTER_BYTES;
        return walked;
    }

    case EX_NameConst: {
        UTA_TRY([[maybe_unused]] const std::int32_t name, reader.readIndex());
        walked.memoryBytes += NAME_BYTES;
        return walked;
    }

    // No operands at all.
    case EX_Stop:
    case EX_Nothing:
    case EX_EndFunctionParms:
    case EX_Self:
    case EX_IntZero:
    case EX_IntOne:
    case EX_True:
    case EX_False:
    case EX_NoObject:
    case EX_IteratorPop:
    case EX_IteratorNext:
        return walked;

    // One nested expression.
    case EX_Return:
    case EX_GotoLabel:
    case EX_EatString:
    case EX_BoolVariable: {
        UTA_CHECK(nested(walked.memoryBytes));
        return walked;
    }

    case EX_Switch: {
        UTA_TRY([[maybe_unused]] const std::uint8_t size, reader.readU8());
        walked.memoryBytes += 1;
        UTA_CHECK(nested(walked.memoryBytes));
        return walked;
    }

    case EX_Jump: {
        UTA_TRY([[maybe_unused]] const std::uint16_t target, reader.readU16());
        walked.memoryBytes += 2;
        return walked;
    }

    // A 16-bit operand, then one nested expression.
    case EX_JumpIfNot:
    case EX_Assert:
    case EX_Skip: {
        UTA_TRY([[maybe_unused]] const std::uint16_t word, reader.readU16());
        walked.memoryBytes += 2;
        UTA_CHECK(nested(walked.memoryBytes));
        return walked;
    }

    // The default case of a switch carries 0xFFFF and no expression.
    case EX_Case: {
        UTA_TRY(const std::uint16_t target, reader.readU16());
        walked.memoryBytes += 2;
        if (target != 0xFFFFu) {
            UTA_CHECK(nested(walked.memoryBytes));
        }
        return walked;
    }

    // A state's label table: pairs of name and offset, ending at `None`.
    // The terminator is a pair like any other and is counted.
    case EX_LabelTable: {
        for (;;) {
            UTA_TRY(const std::int32_t label, reader.readIndex());
            UTA_TRY([[maybe_unused]] const std::uint32_t offset, reader.readU32());
            walked.memoryBytes += NAME_BYTES + 4;
            if (label < 0 ||
                static_cast<std::size_t>(label) >= package.names().size()) {
                return std::unexpected(malformed(
                    "a script label table names index " + std::to_string(label) +
                    ", which is outside the name table"));
            }
            UTA_TRY(const std::string_view labelName,
                    package.name(static_cast<std::uint32_t>(label)));
            if (labelName == "None") {
                return walked;
            }
        }
    }

    // Two nested expressions.
    case EX_Let:
    case EX_DynArrayElement:
    case EX_LetBool:
    case EX_ArrayElement: {
        UTA_CHECK(nested(walked.memoryBytes));
        UTA_CHECK(nested(walked.memoryBytes));
        return walked;
    }

    case EX_New: {
        for (int i = 0; i < 4; ++i) {
            UTA_CHECK(nested(walked.memoryBytes));
        }
        return walked;
    }

    // An expression, then a 16-bit size and an 8-bit width, then the
    // expression the context applies to.
    case EX_ClassContext:
    case EX_Context: {
        UTA_CHECK(nested(walked.memoryBytes));
        UTA_TRY([[maybe_unused]] const std::uint16_t size, reader.readU16());
        UTA_TRY([[maybe_unused]] const std::uint8_t width, reader.readU8());
        walked.memoryBytes += 3;
        UTA_CHECK(nested(walked.memoryBytes));
        return walked;
    }

    // A class or struct reference, then the expression being cast.
    case EX_MetaCast:
    case EX_DynamicCast:
    case EX_StructMember: {
        UTA_TRY([[maybe_unused]] const std::int32_t reference, reader.readIndex());
        walked.memoryBytes += POINTER_BYTES;
        UTA_CHECK(nested(walked.memoryBytes));
        return walked;
    }

    case EX_VirtualFunction: {
        UTA_TRY([[maybe_unused]] const std::int32_t name, reader.readIndex());
        walked.memoryBytes += NAME_BYTES;
        UTA_TRY(const std::size_t parameters, skipParameters(package, reader, depth));
        walked.memoryBytes += parameters;
        return walked;
    }

    case EX_FinalFunction:
    case EX_GlobalFunction: {
        UTA_TRY([[maybe_unused]] const std::int32_t function, reader.readIndex());
        walked.memoryBytes += POINTER_BYTES;
        UTA_TRY(const std::size_t parameters, skipParameters(package, reader, depth));
        walked.memoryBytes += parameters;
        return walked;
    }

    // A 32-bit literal, stored at its full width on disk as well.
    case EX_IntConst:
    case EX_FloatConst: {
        UTA_CHECK(reader.skip(4));
        walked.memoryBytes += 4;
        return walked;
    }

    // A zero-terminated string, one byte per character.
    case EX_StringConst: {
        for (;;) {
            UTA_TRY(const std::uint8_t character, reader.readU8());
            walked.memoryBytes += 1;
            if (character == 0) {
                return walked;
            }
        }
    }

    // The same, two bytes per character.
    case EX_UnicodeStringConst: {
        for (;;) {
            UTA_TRY(const std::uint16_t character, reader.readU16());
            walked.memoryBytes += 2;
            if (character == 0) {
                return walked;
            }
        }
    }

    // Three 32-bit components.
    case EX_RotationConst:
    case EX_VectorConst: {
        UTA_CHECK(reader.skip(12));
        walked.memoryBytes += 12;
        return walked;
    }

    case EX_ByteConst:
    case EX_IntConstByte: {
        UTA_TRY([[maybe_unused]] const std::uint8_t value, reader.readU8());
        walked.memoryBytes += 1;
        return walked;
    }

    // The iterated expression, then the offset past the loop.
    case EX_Iterator: {
        UTA_CHECK(nested(walked.memoryBytes));
        UTA_TRY([[maybe_unused]] const std::uint16_t target, reader.readU16());
        walked.memoryBytes += 2;
        return walked;
    }

    // A struct reference, then the two operands being compared.
    case EX_StructCmpEq:
    case EX_StructCmpNe: {
        UTA_TRY([[maybe_unused]] const std::int32_t structure, reader.readIndex());
        walked.memoryBytes += POINTER_BYTES;
        UTA_CHECK(nested(walked.memoryBytes));
        UTA_CHECK(nested(walked.memoryBytes));
        return walked;
    }

    default:
        if (opcode >= EX_FirstConversion && opcode <= EX_LastConversion) {
            UTA_CHECK(nested(walked.memoryBytes));
            return walked;
        }
        return std::unexpected(malformed("a script carries opcode " + hexByte(opcode) +
                                         ", which this walker does not recognise"));
    }
}

} // namespace

Result<void> skipScript(const Package& package, ByteReader& reader,
                        std::int32_t scriptSize) {
    // First act, and before the walk: a negative size is malformed. Keeping
    // ScriptSize signed to this point is what makes the check possible at all.
    if (scriptSize < 0) {
        return std::unexpected(
            malformed("a class declares a script of " + std::to_string(scriptSize) +
                      " bytes, which is negative"));
    }

    const auto target = static_cast<std::size_t>(scriptSize);
    std::size_t walked = 0;
    // The walk is bounded by the reader's own span, which a caller builds over
    // one export -- so a corrupt ScriptSize runs out of bytes and fails rather
    // than reading on (INV-5).
    while (walked < target) {
        UTA_TRY(const Walked instruction, skipExpression(package, reader, 0));
        walked += instruction.memoryBytes;
    }

    // Landing PAST ScriptSize means an instruction's width was mis-read and
    // the walk stopped mid-instruction. Nothing downstream catches it: a wrong
    // script end still leaves a property list that parses and terminates
    // exactly where it should (SS 2.1), so the exactness rule is the only
    // check there is.
    if (walked != target) {
        return std::unexpected(malformed(
            "a script walk reached " + std::to_string(walked) + " bytes where the class declares " +
            std::to_string(target) + "; an instruction's width was mis-read"));
    }
    return {};
}

} // namespace uta::upkg
