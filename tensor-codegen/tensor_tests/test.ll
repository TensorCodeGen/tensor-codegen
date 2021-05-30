; ModuleID = 'test.c'
source_filename = "test.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: noinline nounwind uwtable
define dso_local <10000 x i32> @foo(<10000 x i32>* byval(<10000 x i32>) align 65536 %0, <10000 x i32>* byval(<10000 x i32>) align 65536 %1) #0 {
  %3 = alloca <10000 x i32>, align 65536
  %4 = alloca <10000 x i32>, align 65536
  %5 = alloca <4 x i32>, align 4
  %6 = alloca <4 x i32>, align 4
  %7 = alloca <4 x i32>, align 4
  %8 = alloca <4 x i32>, align 4
  %9 = alloca <4 x i32>, align 4
  %10 = alloca <4 x i32>, align 4
  %11 = alloca <4 x i32>, align 4
  %12 = alloca i32, align 4
  %13 = alloca <10000 x i32>, align 65536
  %14 = alloca i32, align 4
  %15 = alloca <10000 x i32>, align 65536
  %16 = alloca <10000 x i32>, align 65536
  %17 = alloca i32, align 4
  %18 = alloca <10000 x i32>, align 65536
  %19 = load <10000 x i32>, <10000 x i32>* %0, align 65536
  %20 = load <10000 x i32>, <10000 x i32>* %1, align 65536
  store <10000 x i32> %19, <10000 x i32>* %3, align 65536
  store <10000 x i32> %20, <10000 x i32>* %4, align 65536
  store <4 x i32> <i32 1, i32 1, i32 100, i32 100>, <4 x i32>* %5, align 4
  store <4 x i32> <i32 1, i32 1, i32 100, i32 100>, <4 x i32>* %6, align 4
  store <4 x i32> <i32 1, i32 1, i32 20, i32 20>, <4 x i32>* %7, align 4
  store <4 x i32> <i32 1, i32 1, i32 100, i32 100>, <4 x i32>* %8, align 4
  store <4 x i32> <i32 0, i32 1, i32 2, i32 3>, <4 x i32>* %9, align 4
  store <4 x i32> <i32 0, i32 1, i32 3, i32 2>, <4 x i32>* %10, align 4
  store <4 x i32> zeroinitializer, <4 x i32>* %11, align 4
  %21 = load <10000 x i32>, <10000 x i32>* %3, align 65536
  %22 = load <4 x i32>, <4 x i32>* %5, align 4
  %23 = load <4 x i32>, <4 x i32>* %9, align 4
  %24 = load <4 x i32>, <4 x i32>* %11, align 4
  store <10000 x i32> %21, <10000 x i32>* %13, align 65536
  %25 = call i32 @tensor_typeinfo10000(<10000 x i32>* byval(<10000 x i32>) align 65536 %13, <4 x i32> %22, <4 x i32> %23, <4 x i32> %24)
  store i32 %25, i32* %12, align 4
  %26 = load <10000 x i32>, <10000 x i32>* %4, align 65536
  %27 = load <4 x i32>, <4 x i32>* %6, align 4
  %28 = load <4 x i32>, <4 x i32>* %9, align 4
  %29 = load <4 x i32>, <4 x i32>* %11, align 4
  store <10000 x i32> %26, <10000 x i32>* %15, align 65536
  %30 = call i32 @tensor_typeinfo10000(<10000 x i32>* byval(<10000 x i32>) align 65536 %15, <4 x i32> %27, <4 x i32> %28, <4 x i32> %29)
  store i32 %30, i32* %14, align 4
  %31 = load i32, i32* %12, align 4
  %32 = load i32, i32* %14, align 4
  %33 = call <10000 x i32> @tensor_matmul10000(i32 %31, i32 %32)
  store <10000 x i32> %33, <10000 x i32>* %16, align 65536
  %34 = load <10000 x i32>, <10000 x i32>* %16, align 65536
  %35 = load <4 x i32>, <4 x i32>* %8, align 4
  %36 = load <4 x i32>, <4 x i32>* %9, align 4
  %37 = load <4 x i32>, <4 x i32>* %11, align 4
  store <10000 x i32> %34, <10000 x i32>* %18, align 65536
  %38 = call i32 @tensor_typeinfo10000(<10000 x i32>* byval(<10000 x i32>) align 65536 %18, <4 x i32> %35, <4 x i32> %36, <4 x i32> %37)
  store i32 %38, i32* %17, align 4
  %39 = load <10000 x i32>, <10000 x i32>* %16, align 65536
  ret <10000 x i32> %39
}

declare dso_local i32 @tensor_typeinfo10000(<10000 x i32>* byval(<10000 x i32>) align 65536, <4 x i32>, <4 x i32>, <4 x i32>) #1

declare dso_local <10000 x i32> @tensor_matmul10000(i32, i32) #1

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  %2 = alloca <10000 x i32>, align 65536
  %3 = alloca <10000 x i32>, align 65536
  %4 = alloca <10000 x i32>, align 65536
  %5 = alloca <10000 x i32>, align 65536
  %6 = alloca <10000 x i32>, align 65536
  store i32 0, i32* %1, align 4
  store <10000 x i32> zeroinitializer, <10000 x i32>* %2, align 65536
  store <10000 x i32> zeroinitializer, <10000 x i32>* %3, align 65536
  %7 = load <10000 x i32>, <10000 x i32>* %2, align 65536
  %8 = load <10000 x i32>, <10000 x i32>* %3, align 65536
  store <10000 x i32> %7, <10000 x i32>* %5, align 65536
  store <10000 x i32> %8, <10000 x i32>* %6, align 65536
  %9 = call <10000 x i32> @foo(<10000 x i32>* byval(<10000 x i32>) align 65536 %5, <10000 x i32>* byval(<10000 x i32>) align 65536 %6)
  store <10000 x i32> %9, <10000 x i32>* %4, align 65536
  ret i32 0
}

attributes #0 = { noinline nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "min-legal-vector-width"="320000" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 12.0.0 (https://github.com/AkashIwnK/tensor-codegen.git 7b8ef7eb12f8506d4ad3a147199a39d693aa32b5)"}
