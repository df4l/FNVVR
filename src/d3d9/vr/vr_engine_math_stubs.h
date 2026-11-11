#pragma once

/**
 * Replacements for small functions of the game's math library, written for
 * the 32-bit MSVC conventions they replace: thiscall passes this in ecx and
 * the callee pops its stack arguments, structures come back through a
 * pointer passed as the first stack argument, which is also the result.
 * Only eax, ecx, edx and xmm0-xmm7 are used, which the callers expect to be
 * lost. The arithmetic is single precision, in the order of the originals
 * (findings/engine-optimizations.md, section 10).
 *
 * Every function reads all its inputs before it writes, so a result may
 * overlap an operand as it may with the originals.
 */

namespace dxvk {

  namespace engine_math {

    /**
     * NiPoint3::NiPoint3(x, y, z), thiscall, returns this
     *
     * Why: it is the hottest of the group (called for every temporary
     * vector) and its body is only three copies. The original builds a
     * stack frame and moves the floats through the x87 stack.
     * How: the arguments are copied as 32-bit integers, so no float
     * register is involved and no value is converted. NaN payloads and
     * signed zeros are kept exactly as passed.
     */
    __attribute__((naked)) static void pointConstruct() {
      asm volatile(
        "movl 4(%esp), %eax\n\t"
        "movl %eax, (%ecx)\n\t"
        "movl 8(%esp), %eax\n\t"
        "movl %eax, 4(%ecx)\n\t"
        "movl 12(%esp), %eax\n\t"
        "movl %eax, 8(%ecx)\n\t"
        "movl %ecx, %eax\n\t"
        "ret $12\n\t");
    }

    /**
     * NiPoint3::operator+(out, other), thiscall, returns out
     *
     * Why: three float additions that the original does through a frame
     * and x87 loads and stores.
     * How: one scalar SSE addition per component (addss), same operand
     * order. The three sums are held in registers and stored last, so
     * out may be this or other.
     */
    __attribute__((naked)) static void pointAdd() {
      asm volatile(
        "movl 4(%esp), %eax\n\t"
        "movl 8(%esp), %edx\n\t"
        "movss (%ecx), %xmm0\n\t"
        "addss (%edx), %xmm0\n\t"
        "movss 4(%ecx), %xmm1\n\t"
        "addss 4(%edx), %xmm1\n\t"
        "movss 8(%ecx), %xmm2\n\t"
        "addss 8(%edx), %xmm2\n\t"
        "movss %xmm0, (%eax)\n\t"
        "movss %xmm1, 4(%eax)\n\t"
        "movss %xmm2, 8(%eax)\n\t"
        "ret $8\n\t");
    }

    /**
     * NiPoint3::operator-(out, other), thiscall, returns out
     *
     * Why and how: as the sum, with subss. this is the left operand
     * (this - other), and all reads happen before the stores, so out may
     * overlap either operand.
     */
    __attribute__((naked)) static void pointSubtract() {
      asm volatile(
        "movl 4(%esp), %eax\n\t"
        "movl 8(%esp), %edx\n\t"
        "movss (%ecx), %xmm0\n\t"
        "subss (%edx), %xmm0\n\t"
        "movss 4(%ecx), %xmm1\n\t"
        "subss 4(%edx), %xmm1\n\t"
        "movss 8(%ecx), %xmm2\n\t"
        "subss 8(%edx), %xmm2\n\t"
        "movss %xmm0, (%eax)\n\t"
        "movss %xmm1, 4(%eax)\n\t"
        "movss %xmm2, 8(%eax)\n\t"
        "ret $8\n\t");
    }

    /**
     * operator*(out, scale, point), cdecl, returns out
     *
     * Why: scaling a vector is as common as the sum, and the original
     * pays the same frame and x87 traffic.
     * How: the scale is loaded once into xmm3 and multiplies the three
     * components. The caller removes the arguments (cdecl), so the stub
     * ends with a plain ret, not ret $n. Stores come last, out may be
     * point.
     */
    __attribute__((naked)) static void pointScale() {
      asm volatile(
        "movl 4(%esp), %eax\n\t"
        "movl 12(%esp), %edx\n\t"
        "movss 8(%esp), %xmm3\n\t"
        "movss (%edx), %xmm0\n\t"
        "mulss %xmm3, %xmm0\n\t"
        "movss 4(%edx), %xmm1\n\t"
        "mulss %xmm3, %xmm1\n\t"
        "movss 8(%edx), %xmm2\n\t"
        "mulss %xmm3, %xmm2\n\t"
        "movss %xmm0, (%eax)\n\t"
        "movss %xmm1, 4(%eax)\n\t"
        "movss %xmm2, 8(%eax)\n\t"
        "ret\n\t");
    }

    /**
     * NiMatrix3::operator*(out, point), thiscall, returns out
     *
     * Why: rotating a vector by a matrix is the core of every node
     * transform update in the scene graph.
     * How: the vector is loaded once and each output is the row dot
     * product, summed left to right ((a*x + b*y) + c*z) as the original
     * does, so the result is bit for bit the same. The three results stay
     * in xmm3-xmm5 until the end, which makes an out that overlaps point
     * safe without a temporary in memory.
     */
    __attribute__((naked)) static void matrixPoint() {
      asm volatile(
        "movl 8(%esp), %edx\n\t"
        "movss (%edx), %xmm0\n\t"
        "movss 4(%edx), %xmm1\n\t"
        "movss 8(%edx), %xmm2\n\t"
        "movss 0(%ecx), %xmm3\n\t"
        "mulss %xmm0, %xmm3\n\t"
        "movss 4(%ecx), %xmm6\n\t"
        "mulss %xmm1, %xmm6\n\t"
        "addss %xmm6, %xmm3\n\t"
        "movss 8(%ecx), %xmm6\n\t"
        "mulss %xmm2, %xmm6\n\t"
        "addss %xmm6, %xmm3\n\t"
        "movss 12(%ecx), %xmm4\n\t"
        "mulss %xmm0, %xmm4\n\t"
        "movss 16(%ecx), %xmm6\n\t"
        "mulss %xmm1, %xmm6\n\t"
        "addss %xmm6, %xmm4\n\t"
        "movss 20(%ecx), %xmm6\n\t"
        "mulss %xmm2, %xmm6\n\t"
        "addss %xmm6, %xmm4\n\t"
        "movss 24(%ecx), %xmm5\n\t"
        "mulss %xmm0, %xmm5\n\t"
        "movss 28(%ecx), %xmm6\n\t"
        "mulss %xmm1, %xmm6\n\t"
        "addss %xmm6, %xmm5\n\t"
        "movss 32(%ecx), %xmm6\n\t"
        "mulss %xmm2, %xmm6\n\t"
        "addss %xmm6, %xmm5\n\t"
        "movl 4(%esp), %eax\n\t"
        "movss %xmm3, (%eax)\n\t"
        "movss %xmm4, 4(%eax)\n\t"
        "movss %xmm5, 8(%eax)\n\t"
        "ret $8\n\t");
    }

    /**
     * NiMatrix3::operator*(out, matrix), thiscall, returns out
     *
     * Why: 27 multiplications that the original does through x87 with
     * every partial result stored to memory.
     * How: each element is the row by column dot product, summed in the
     * original's order. Nine results do not fit the registers that are
     * free, so they are built in a 36-byte stack temporary and copied to
     * out at the end as integers. That copy is what keeps out = this or
     * out = matrix correct (a product in place needs the old values until
     * the last element is computed).
     */
    __attribute__((naked)) static void matrixMatrix() {
      asm volatile(
        "movl 4(%esp), %eax\n\t"
        "movl 8(%esp), %edx\n\t"
        "subl $36, %esp\n\t"
        "movss 0(%ecx), %xmm0\n\t"
        "mulss 0(%edx), %xmm0\n\t"
        "movss 4(%ecx), %xmm1\n\t"
        "mulss 12(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 8(%ecx), %xmm1\n\t"
        "mulss 24(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 0(%esp)\n\t"
        "movss 0(%ecx), %xmm0\n\t"
        "mulss 4(%edx), %xmm0\n\t"
        "movss 4(%ecx), %xmm1\n\t"
        "mulss 16(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 8(%ecx), %xmm1\n\t"
        "mulss 28(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 4(%esp)\n\t"
        "movss 0(%ecx), %xmm0\n\t"
        "mulss 8(%edx), %xmm0\n\t"
        "movss 4(%ecx), %xmm1\n\t"
        "mulss 20(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 8(%ecx), %xmm1\n\t"
        "mulss 32(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 8(%esp)\n\t"
        "movss 12(%ecx), %xmm0\n\t"
        "mulss 0(%edx), %xmm0\n\t"
        "movss 16(%ecx), %xmm1\n\t"
        "mulss 12(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 20(%ecx), %xmm1\n\t"
        "mulss 24(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 12(%esp)\n\t"
        "movss 12(%ecx), %xmm0\n\t"
        "mulss 4(%edx), %xmm0\n\t"
        "movss 16(%ecx), %xmm1\n\t"
        "mulss 16(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 20(%ecx), %xmm1\n\t"
        "mulss 28(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 16(%esp)\n\t"
        "movss 12(%ecx), %xmm0\n\t"
        "mulss 8(%edx), %xmm0\n\t"
        "movss 16(%ecx), %xmm1\n\t"
        "mulss 20(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 20(%ecx), %xmm1\n\t"
        "mulss 32(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 20(%esp)\n\t"
        "movss 24(%ecx), %xmm0\n\t"
        "mulss 0(%edx), %xmm0\n\t"
        "movss 28(%ecx), %xmm1\n\t"
        "mulss 12(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 32(%ecx), %xmm1\n\t"
        "mulss 24(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 24(%esp)\n\t"
        "movss 24(%ecx), %xmm0\n\t"
        "mulss 4(%edx), %xmm0\n\t"
        "movss 28(%ecx), %xmm1\n\t"
        "mulss 16(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 32(%ecx), %xmm1\n\t"
        "mulss 28(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 28(%esp)\n\t"
        "movss 24(%ecx), %xmm0\n\t"
        "mulss 8(%edx), %xmm0\n\t"
        "movss 28(%ecx), %xmm1\n\t"
        "mulss 20(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 32(%ecx), %xmm1\n\t"
        "mulss 32(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 32(%esp)\n\t"
        "movl 0(%esp), %ecx\n\t"
        "movl %ecx, 0(%eax)\n\t"
        "movl 4(%esp), %ecx\n\t"
        "movl %ecx, 4(%eax)\n\t"
        "movl 8(%esp), %ecx\n\t"
        "movl %ecx, 8(%eax)\n\t"
        "movl 12(%esp), %ecx\n\t"
        "movl %ecx, 12(%eax)\n\t"
        "movl 16(%esp), %ecx\n\t"
        "movl %ecx, 16(%eax)\n\t"
        "movl 20(%esp), %ecx\n\t"
        "movl %ecx, 20(%eax)\n\t"
        "movl 24(%esp), %ecx\n\t"
        "movl %ecx, 24(%eax)\n\t"
        "movl 28(%esp), %ecx\n\t"
        "movl %ecx, 28(%eax)\n\t"
        "movl 32(%esp), %ecx\n\t"
        "movl %ecx, 32(%eax)\n\t"
        "addl $36, %esp\n\t"
        "ret $8\n\t");
    }

    /**
     * NiTransform::operator*(out, transform), thiscall, returns out
     *
     * Why: it is called for every node of every skeleton and hierarchy,
     * and it chains a matrix product and a matrix by vector product.
     * How: the same code as the two functions above, inlined in one stub,
     * so there is no nested call and no second frame. Rotation is
     * R1 * R2, translation is T1 + s1 * (R1 * T2), scale is s1 * s2. All
     * 13 floats go to a 52-byte stack temporary and are copied to out at
     * the end, so out may be this or transform.
     * Layout: rotation at 0, translation at 0x24, scale at 0x30, size 0x34.
     */
    __attribute__((naked)) static void transformTransform() {
      asm volatile(
        "movl 4(%esp), %eax\n\t"
        "movl 8(%esp), %edx\n\t"
        "subl $52, %esp\n\t"
        "movss 0(%ecx), %xmm0\n\t"
        "mulss 0(%edx), %xmm0\n\t"
        "movss 4(%ecx), %xmm1\n\t"
        "mulss 12(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 8(%ecx), %xmm1\n\t"
        "mulss 24(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 0(%esp)\n\t"
        "movss 0(%ecx), %xmm0\n\t"
        "mulss 4(%edx), %xmm0\n\t"
        "movss 4(%ecx), %xmm1\n\t"
        "mulss 16(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 8(%ecx), %xmm1\n\t"
        "mulss 28(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 4(%esp)\n\t"
        "movss 0(%ecx), %xmm0\n\t"
        "mulss 8(%edx), %xmm0\n\t"
        "movss 4(%ecx), %xmm1\n\t"
        "mulss 20(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 8(%ecx), %xmm1\n\t"
        "mulss 32(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 8(%esp)\n\t"
        "movss 12(%ecx), %xmm0\n\t"
        "mulss 0(%edx), %xmm0\n\t"
        "movss 16(%ecx), %xmm1\n\t"
        "mulss 12(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 20(%ecx), %xmm1\n\t"
        "mulss 24(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 12(%esp)\n\t"
        "movss 12(%ecx), %xmm0\n\t"
        "mulss 4(%edx), %xmm0\n\t"
        "movss 16(%ecx), %xmm1\n\t"
        "mulss 16(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 20(%ecx), %xmm1\n\t"
        "mulss 28(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 16(%esp)\n\t"
        "movss 12(%ecx), %xmm0\n\t"
        "mulss 8(%edx), %xmm0\n\t"
        "movss 16(%ecx), %xmm1\n\t"
        "mulss 20(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 20(%ecx), %xmm1\n\t"
        "mulss 32(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 20(%esp)\n\t"
        "movss 24(%ecx), %xmm0\n\t"
        "mulss 0(%edx), %xmm0\n\t"
        "movss 28(%ecx), %xmm1\n\t"
        "mulss 12(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 32(%ecx), %xmm1\n\t"
        "mulss 24(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 24(%esp)\n\t"
        "movss 24(%ecx), %xmm0\n\t"
        "mulss 4(%edx), %xmm0\n\t"
        "movss 28(%ecx), %xmm1\n\t"
        "mulss 16(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 32(%ecx), %xmm1\n\t"
        "mulss 28(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 28(%esp)\n\t"
        "movss 24(%ecx), %xmm0\n\t"
        "mulss 8(%edx), %xmm0\n\t"
        "movss 28(%ecx), %xmm1\n\t"
        "mulss 20(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss 32(%ecx), %xmm1\n\t"
        "mulss 32(%edx), %xmm1\n\t"
        "addss %xmm1, %xmm0\n\t"
        "movss %xmm0, 32(%esp)\n\t"
        "movss 36(%edx), %xmm0\n\t"
        "movss 40(%edx), %xmm1\n\t"
        "movss 44(%edx), %xmm2\n\t"
        "movss 0(%ecx), %xmm3\n\t"
        "mulss %xmm0, %xmm3\n\t"
        "movss 4(%ecx), %xmm6\n\t"
        "mulss %xmm1, %xmm6\n\t"
        "addss %xmm6, %xmm3\n\t"
        "movss 8(%ecx), %xmm6\n\t"
        "mulss %xmm2, %xmm6\n\t"
        "addss %xmm6, %xmm3\n\t"
        "movss 12(%ecx), %xmm4\n\t"
        "mulss %xmm0, %xmm4\n\t"
        "movss 16(%ecx), %xmm6\n\t"
        "mulss %xmm1, %xmm6\n\t"
        "addss %xmm6, %xmm4\n\t"
        "movss 20(%ecx), %xmm6\n\t"
        "mulss %xmm2, %xmm6\n\t"
        "addss %xmm6, %xmm4\n\t"
        "movss 24(%ecx), %xmm5\n\t"
        "mulss %xmm0, %xmm5\n\t"
        "movss 28(%ecx), %xmm6\n\t"
        "mulss %xmm1, %xmm6\n\t"
        "addss %xmm6, %xmm5\n\t"
        "movss 32(%ecx), %xmm6\n\t"
        "mulss %xmm2, %xmm6\n\t"
        "addss %xmm6, %xmm5\n\t"
        "movss 48(%ecx), %xmm7\n\t"
        "mulss %xmm7, %xmm3\n\t"
        "movss 36(%ecx), %xmm0\n\t"
        "addss %xmm3, %xmm0\n\t"
        "movss %xmm0, 36(%esp)\n\t"
        "mulss %xmm7, %xmm4\n\t"
        "movss 40(%ecx), %xmm0\n\t"
        "addss %xmm4, %xmm0\n\t"
        "movss %xmm0, 40(%esp)\n\t"
        "mulss %xmm7, %xmm5\n\t"
        "movss 44(%ecx), %xmm0\n\t"
        "addss %xmm5, %xmm0\n\t"
        "movss %xmm0, 44(%esp)\n\t"
        "mulss 48(%edx), %xmm7\n\t"
        "movss %xmm7, 48(%esp)\n\t"
        "movl 0(%esp), %ecx\n\t"
        "movl %ecx, 0(%eax)\n\t"
        "movl 4(%esp), %ecx\n\t"
        "movl %ecx, 4(%eax)\n\t"
        "movl 8(%esp), %ecx\n\t"
        "movl %ecx, 8(%eax)\n\t"
        "movl 12(%esp), %ecx\n\t"
        "movl %ecx, 12(%eax)\n\t"
        "movl 16(%esp), %ecx\n\t"
        "movl %ecx, 16(%eax)\n\t"
        "movl 20(%esp), %ecx\n\t"
        "movl %ecx, 20(%eax)\n\t"
        "movl 24(%esp), %ecx\n\t"
        "movl %ecx, 24(%eax)\n\t"
        "movl 28(%esp), %ecx\n\t"
        "movl %ecx, 28(%eax)\n\t"
        "movl 32(%esp), %ecx\n\t"
        "movl %ecx, 32(%eax)\n\t"
        "movl 36(%esp), %ecx\n\t"
        "movl %ecx, 36(%eax)\n\t"
        "movl 40(%esp), %ecx\n\t"
        "movl %ecx, 40(%eax)\n\t"
        "movl 44(%esp), %ecx\n\t"
        "movl %ecx, 44(%eax)\n\t"
        "movl 48(%esp), %ecx\n\t"
        "movl %ecx, 48(%eax)\n\t"
        "addl $52, %esp\n\t"
        "ret $8\n\t");
    }

    using Function = void (*)();

    constexpr Function PointConstruct      = pointConstruct;
    constexpr Function PointAdd            = pointAdd;
    constexpr Function PointSubtract       = pointSubtract;
    constexpr Function PointScale          = pointScale;
    constexpr Function MatrixPoint         = matrixPoint;
    constexpr Function MatrixMatrix        = matrixMatrix;
    constexpr Function TransformTransform  = transformTransform;

  }

}
