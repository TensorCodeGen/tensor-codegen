; ModuleID = 'test.ll'
source_filename = "test.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: noinline nounwind uwtable
define dso_local <10000 x i32> @foo(<10000 x i32>* byval(<10000 x i32>) align 65536 %0, <10000 x i32>* byval(<10000 x i32>) align 65536 %1) #0 {
  %malloccall1 = tail call i8* @malloc(i32 40000)
  %3 = bitcast i8* %malloccall1 to i32*
  %malloccall = tail call i8* @malloc(i32 40000)
  %4 = bitcast i8* %malloccall to i32*
  %malloccall3 = tail call i8* @malloc(i32 40000)
  %5 = bitcast i8* %malloccall3 to i32*
  %load.cast2 = bitcast <10000 x i32>* %0 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i32(i8* %malloccall1, i8* %load.cast2, i32 40000, i1 false)
  %load.cast = bitcast <10000 x i32>* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i32(i8* %malloccall, i8* %load.cast, i32 40000, i1 false)
  br label %loop.header

loop.header:                                      ; preds = %2, %loop.latch
  %loop.iv = phi i32 [ 0, %2 ], [ %loop.step, %loop.latch ]
  %input.stride27 = mul i32 %loop.iv, 10000
  %input.stride47 = mul i32 %loop.iv, 10000
  %input.stride237 = mul i32 %loop.iv, 10000
  br label %loop.header5

loop.header5:                                     ; preds = %loop.header, %loop.latch6
  %loop.iv7 = phi i32 [ 0, %loop.header ], [ %loop.step8, %loop.latch6 ]
  %input.stride25 = mul i32 %loop.iv7, 10000
  %input.stride45 = mul i32 %loop.iv7, 10000
  %input.stride235 = mul i32 %loop.iv7, 10000
  br label %loop.header10

loop.header10:                                    ; preds = %loop.header5, %loop.latch11
  %loop.iv12 = phi i32 [ 0, %loop.header5 ], [ %loop.step13, %loop.latch11 ]
  %input.stride = mul i32 %loop.iv12, 100
  %input.stride233 = mul i32 %loop.iv12, 100
  br label %loop.header15

loop.header15:                                    ; preds = %loop.header10, %loop.latch16
  %loop.iv17 = phi i32 [ 0, %loop.header10 ], [ %loop.step18, %loop.latch16 ]
  br label %loop.header20

loop.header20:                                    ; preds = %loop.header15, %loop.latch21
  %loop.iv22 = phi i32 [ 0, %loop.header15 ], [ %loop.step23, %loop.latch21 ]
  %result.vec.0 = phi <5 x i32> [ zeroinitializer, %loop.header15 ], [ %acc.vector96, %loop.latch21 ]
  %result.vec.1 = phi <5 x i32> [ zeroinitializer, %loop.header15 ], [ %acc.vector130, %loop.latch21 ]
  %result.vec.2 = phi <5 x i32> [ zeroinitializer, %loop.header15 ], [ %acc.vector164, %loop.latch21 ]
  %result.vec.3 = phi <5 x i32> [ zeroinitializer, %loop.header15 ], [ %acc.vector198, %loop.latch21 ]
  %result.vec.4 = phi <5 x i32> [ zeroinitializer, %loop.header15 ], [ %acc.vector232, %loop.latch21 ]
  br label %loop.body

loop.body:                                        ; preds = %loop.header20
  %input.offset = add i32 %input.stride, %loop.iv22
  %input.offset26 = add i32 %input.stride25, %input.offset
  %input.offset28 = add i32 %input.stride27, %input.offset26
  %tile.start = getelementptr i32, i32* %3, i32 %input.offset28
  %vec.cast = bitcast i32* %tile.start to <5 x i32>*
  %row.load = load <5 x i32>, <5 x i32>* %vec.cast, align 4
  %vec.gep = getelementptr i32, i32* %tile.start, i32 100
  %vec.cast29 = bitcast i32* %vec.gep to <5 x i32>*
  %row.load30 = load <5 x i32>, <5 x i32>* %vec.cast29, align 4
  %vec.gep32 = getelementptr i32, i32* %tile.start, i32 200
  %vec.cast33 = bitcast i32* %vec.gep32 to <5 x i32>*
  %row.load34 = load <5 x i32>, <5 x i32>* %vec.cast33, align 4
  %vec.gep36 = getelementptr i32, i32* %tile.start, i32 300
  %vec.cast37 = bitcast i32* %vec.gep36 to <5 x i32>*
  %row.load38 = load <5 x i32>, <5 x i32>* %vec.cast37, align 4
  %vec.gep40 = getelementptr i32, i32* %tile.start, i32 400
  %vec.cast41 = bitcast i32* %vec.gep40 to <5 x i32>*
  %row.load42 = load <5 x i32>, <5 x i32>* %vec.cast41, align 4
  %input.stride43 = mul i32 %loop.iv22, 100
  %input.offset44 = add i32 %input.stride43, %loop.iv17
  %input.offset46 = add i32 %input.stride45, %input.offset44
  %input.offset48 = add i32 %input.stride47, %input.offset46
  %tile.start49 = getelementptr i32, i32* %4, i32 %input.offset48
  %vec.cast50 = bitcast i32* %tile.start49 to <5 x i32>*
  %row.load51 = load <5 x i32>, <5 x i32>* %vec.cast50, align 4
  %vec.gep53 = getelementptr i32, i32* %tile.start49, i32 100
  %vec.cast54 = bitcast i32* %vec.gep53 to <5 x i32>*
  %row.load55 = load <5 x i32>, <5 x i32>* %vec.cast54, align 4
  %vec.gep57 = getelementptr i32, i32* %tile.start49, i32 200
  %vec.cast58 = bitcast i32* %vec.gep57 to <5 x i32>*
  %row.load59 = load <5 x i32>, <5 x i32>* %vec.cast58, align 4
  %vec.gep61 = getelementptr i32, i32* %tile.start49, i32 300
  %vec.cast62 = bitcast i32* %vec.gep61 to <5 x i32>*
  %row.load63 = load <5 x i32>, <5 x i32>* %vec.cast62, align 4
  %vec.gep65 = getelementptr i32, i32* %tile.start49, i32 400
  %vec.cast66 = bitcast i32* %vec.gep65 to <5 x i32>*
  %row.load67 = load <5 x i32>, <5 x i32>* %vec.cast66, align 4
  %block = shufflevector <5 x i32> %row.load51, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %6 = extractelement <5 x i32> %row.load, i32 0
  %broadcast.insert = insertelement <4 x i32> poison, i32 %6, i32 0
  %broadcast = shufflevector <4 x i32> %broadcast.insert, <4 x i32> poison, <4 x i32> zeroinitializer
  %7 = mul <4 x i32> %broadcast, %block
  %block68 = shufflevector <5 x i32> %row.load55, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %8 = extractelement <5 x i32> %row.load, i32 1
  %broadcast.insert69 = insertelement <4 x i32> poison, i32 %8, i32 0
  %broadcast70 = shufflevector <4 x i32> %broadcast.insert69, <4 x i32> poison, <4 x i32> zeroinitializer
  %9 = mul <4 x i32> %broadcast70, %block68
  %10 = add <4 x i32> %7, %9
  %block71 = shufflevector <5 x i32> %row.load59, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %11 = extractelement <5 x i32> %row.load, i32 2
  %broadcast.insert72 = insertelement <4 x i32> poison, i32 %11, i32 0
  %broadcast73 = shufflevector <4 x i32> %broadcast.insert72, <4 x i32> poison, <4 x i32> zeroinitializer
  %12 = mul <4 x i32> %broadcast73, %block71
  %13 = add <4 x i32> %10, %12
  %block74 = shufflevector <5 x i32> %row.load63, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %14 = extractelement <5 x i32> %row.load, i32 3
  %broadcast.insert75 = insertelement <4 x i32> poison, i32 %14, i32 0
  %broadcast76 = shufflevector <4 x i32> %broadcast.insert75, <4 x i32> poison, <4 x i32> zeroinitializer
  %15 = mul <4 x i32> %broadcast76, %block74
  %16 = add <4 x i32> %13, %15
  %block77 = shufflevector <5 x i32> %row.load67, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %17 = extractelement <5 x i32> %row.load, i32 4
  %broadcast.insert78 = insertelement <4 x i32> poison, i32 %17, i32 0
  %broadcast79 = shufflevector <4 x i32> %broadcast.insert78, <4 x i32> poison, <4 x i32> zeroinitializer
  %18 = mul <4 x i32> %broadcast79, %block77
  %19 = add <4 x i32> %16, %18
  %20 = shufflevector <4 x i32> %19, <4 x i32> poison, <5 x i32> <i32 0, i32 1, i32 2, i32 3, i32 undef>
  %tile.vect = shufflevector <5 x i32> %result.vec.0, <5 x i32> %20, <5 x i32> <i32 5, i32 6, i32 7, i32 8, i32 4>
  %acc.vector = add <5 x i32> %result.vec.0, %tile.vect
  %block80 = shufflevector <5 x i32> %row.load51, <5 x i32> poison, <1 x i32> <i32 4>
  %21 = extractelement <5 x i32> %row.load, i32 0
  %broadcast.insert81 = insertelement <1 x i32> poison, i32 %21, i32 0
  %broadcast82 = shufflevector <1 x i32> %broadcast.insert81, <1 x i32> poison, <1 x i32> zeroinitializer
  %22 = mul <1 x i32> %broadcast82, %block80
  %block83 = shufflevector <5 x i32> %row.load55, <5 x i32> poison, <1 x i32> <i32 4>
  %23 = extractelement <5 x i32> %row.load, i32 1
  %broadcast.insert84 = insertelement <1 x i32> poison, i32 %23, i32 0
  %broadcast85 = shufflevector <1 x i32> %broadcast.insert84, <1 x i32> poison, <1 x i32> zeroinitializer
  %24 = mul <1 x i32> %broadcast85, %block83
  %25 = add <1 x i32> %22, %24
  %block86 = shufflevector <5 x i32> %row.load59, <5 x i32> poison, <1 x i32> <i32 4>
  %26 = extractelement <5 x i32> %row.load, i32 2
  %broadcast.insert87 = insertelement <1 x i32> poison, i32 %26, i32 0
  %broadcast88 = shufflevector <1 x i32> %broadcast.insert87, <1 x i32> poison, <1 x i32> zeroinitializer
  %27 = mul <1 x i32> %broadcast88, %block86
  %28 = add <1 x i32> %25, %27
  %block89 = shufflevector <5 x i32> %row.load63, <5 x i32> poison, <1 x i32> <i32 4>
  %29 = extractelement <5 x i32> %row.load, i32 3
  %broadcast.insert90 = insertelement <1 x i32> poison, i32 %29, i32 0
  %broadcast91 = shufflevector <1 x i32> %broadcast.insert90, <1 x i32> poison, <1 x i32> zeroinitializer
  %30 = mul <1 x i32> %broadcast91, %block89
  %31 = add <1 x i32> %28, %30
  %block92 = shufflevector <5 x i32> %row.load67, <5 x i32> poison, <1 x i32> <i32 4>
  %32 = extractelement <5 x i32> %row.load, i32 4
  %broadcast.insert93 = insertelement <1 x i32> poison, i32 %32, i32 0
  %broadcast94 = shufflevector <1 x i32> %broadcast.insert93, <1 x i32> poison, <1 x i32> zeroinitializer
  %33 = mul <1 x i32> %broadcast94, %block92
  %34 = add <1 x i32> %31, %33
  %35 = shufflevector <1 x i32> %34, <1 x i32> poison, <5 x i32> <i32 0, i32 undef, i32 undef, i32 undef, i32 undef>
  %tile.vect95 = shufflevector <5 x i32> %acc.vector, <5 x i32> %35, <5 x i32> <i32 0, i32 1, i32 2, i32 3, i32 5>
  %acc.vector96 = add <5 x i32> %acc.vector, %tile.vect95
  %block97 = shufflevector <5 x i32> %row.load51, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %36 = extractelement <5 x i32> %row.load30, i32 0
  %broadcast.insert98 = insertelement <4 x i32> poison, i32 %36, i32 0
  %broadcast99 = shufflevector <4 x i32> %broadcast.insert98, <4 x i32> poison, <4 x i32> zeroinitializer
  %37 = mul <4 x i32> %broadcast99, %block97
  %block100 = shufflevector <5 x i32> %row.load55, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %38 = extractelement <5 x i32> %row.load30, i32 1
  %broadcast.insert101 = insertelement <4 x i32> poison, i32 %38, i32 0
  %broadcast102 = shufflevector <4 x i32> %broadcast.insert101, <4 x i32> poison, <4 x i32> zeroinitializer
  %39 = mul <4 x i32> %broadcast102, %block100
  %40 = add <4 x i32> %37, %39
  %block103 = shufflevector <5 x i32> %row.load59, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %41 = extractelement <5 x i32> %row.load30, i32 2
  %broadcast.insert104 = insertelement <4 x i32> poison, i32 %41, i32 0
  %broadcast105 = shufflevector <4 x i32> %broadcast.insert104, <4 x i32> poison, <4 x i32> zeroinitializer
  %42 = mul <4 x i32> %broadcast105, %block103
  %43 = add <4 x i32> %40, %42
  %block106 = shufflevector <5 x i32> %row.load63, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %44 = extractelement <5 x i32> %row.load30, i32 3
  %broadcast.insert107 = insertelement <4 x i32> poison, i32 %44, i32 0
  %broadcast108 = shufflevector <4 x i32> %broadcast.insert107, <4 x i32> poison, <4 x i32> zeroinitializer
  %45 = mul <4 x i32> %broadcast108, %block106
  %46 = add <4 x i32> %43, %45
  %block109 = shufflevector <5 x i32> %row.load67, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %47 = extractelement <5 x i32> %row.load30, i32 4
  %broadcast.insert110 = insertelement <4 x i32> poison, i32 %47, i32 0
  %broadcast111 = shufflevector <4 x i32> %broadcast.insert110, <4 x i32> poison, <4 x i32> zeroinitializer
  %48 = mul <4 x i32> %broadcast111, %block109
  %49 = add <4 x i32> %46, %48
  %50 = shufflevector <4 x i32> %49, <4 x i32> poison, <5 x i32> <i32 0, i32 1, i32 2, i32 3, i32 undef>
  %tile.vect112 = shufflevector <5 x i32> %result.vec.1, <5 x i32> %50, <5 x i32> <i32 5, i32 6, i32 7, i32 8, i32 4>
  %acc.vector113 = add <5 x i32> %result.vec.1, %tile.vect112
  %block114 = shufflevector <5 x i32> %row.load51, <5 x i32> poison, <1 x i32> <i32 4>
  %51 = extractelement <5 x i32> %row.load30, i32 0
  %broadcast.insert115 = insertelement <1 x i32> poison, i32 %51, i32 0
  %broadcast116 = shufflevector <1 x i32> %broadcast.insert115, <1 x i32> poison, <1 x i32> zeroinitializer
  %52 = mul <1 x i32> %broadcast116, %block114
  %block117 = shufflevector <5 x i32> %row.load55, <5 x i32> poison, <1 x i32> <i32 4>
  %53 = extractelement <5 x i32> %row.load30, i32 1
  %broadcast.insert118 = insertelement <1 x i32> poison, i32 %53, i32 0
  %broadcast119 = shufflevector <1 x i32> %broadcast.insert118, <1 x i32> poison, <1 x i32> zeroinitializer
  %54 = mul <1 x i32> %broadcast119, %block117
  %55 = add <1 x i32> %52, %54
  %block120 = shufflevector <5 x i32> %row.load59, <5 x i32> poison, <1 x i32> <i32 4>
  %56 = extractelement <5 x i32> %row.load30, i32 2
  %broadcast.insert121 = insertelement <1 x i32> poison, i32 %56, i32 0
  %broadcast122 = shufflevector <1 x i32> %broadcast.insert121, <1 x i32> poison, <1 x i32> zeroinitializer
  %57 = mul <1 x i32> %broadcast122, %block120
  %58 = add <1 x i32> %55, %57
  %block123 = shufflevector <5 x i32> %row.load63, <5 x i32> poison, <1 x i32> <i32 4>
  %59 = extractelement <5 x i32> %row.load30, i32 3
  %broadcast.insert124 = insertelement <1 x i32> poison, i32 %59, i32 0
  %broadcast125 = shufflevector <1 x i32> %broadcast.insert124, <1 x i32> poison, <1 x i32> zeroinitializer
  %60 = mul <1 x i32> %broadcast125, %block123
  %61 = add <1 x i32> %58, %60
  %block126 = shufflevector <5 x i32> %row.load67, <5 x i32> poison, <1 x i32> <i32 4>
  %62 = extractelement <5 x i32> %row.load30, i32 4
  %broadcast.insert127 = insertelement <1 x i32> poison, i32 %62, i32 0
  %broadcast128 = shufflevector <1 x i32> %broadcast.insert127, <1 x i32> poison, <1 x i32> zeroinitializer
  %63 = mul <1 x i32> %broadcast128, %block126
  %64 = add <1 x i32> %61, %63
  %65 = shufflevector <1 x i32> %64, <1 x i32> poison, <5 x i32> <i32 0, i32 undef, i32 undef, i32 undef, i32 undef>
  %tile.vect129 = shufflevector <5 x i32> %acc.vector113, <5 x i32> %65, <5 x i32> <i32 0, i32 1, i32 2, i32 3, i32 5>
  %acc.vector130 = add <5 x i32> %acc.vector113, %tile.vect129
  %block131 = shufflevector <5 x i32> %row.load51, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %66 = extractelement <5 x i32> %row.load34, i32 0
  %broadcast.insert132 = insertelement <4 x i32> poison, i32 %66, i32 0
  %broadcast133 = shufflevector <4 x i32> %broadcast.insert132, <4 x i32> poison, <4 x i32> zeroinitializer
  %67 = mul <4 x i32> %broadcast133, %block131
  %block134 = shufflevector <5 x i32> %row.load55, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %68 = extractelement <5 x i32> %row.load34, i32 1
  %broadcast.insert135 = insertelement <4 x i32> poison, i32 %68, i32 0
  %broadcast136 = shufflevector <4 x i32> %broadcast.insert135, <4 x i32> poison, <4 x i32> zeroinitializer
  %69 = mul <4 x i32> %broadcast136, %block134
  %70 = add <4 x i32> %67, %69
  %block137 = shufflevector <5 x i32> %row.load59, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %71 = extractelement <5 x i32> %row.load34, i32 2
  %broadcast.insert138 = insertelement <4 x i32> poison, i32 %71, i32 0
  %broadcast139 = shufflevector <4 x i32> %broadcast.insert138, <4 x i32> poison, <4 x i32> zeroinitializer
  %72 = mul <4 x i32> %broadcast139, %block137
  %73 = add <4 x i32> %70, %72
  %block140 = shufflevector <5 x i32> %row.load63, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %74 = extractelement <5 x i32> %row.load34, i32 3
  %broadcast.insert141 = insertelement <4 x i32> poison, i32 %74, i32 0
  %broadcast142 = shufflevector <4 x i32> %broadcast.insert141, <4 x i32> poison, <4 x i32> zeroinitializer
  %75 = mul <4 x i32> %broadcast142, %block140
  %76 = add <4 x i32> %73, %75
  %block143 = shufflevector <5 x i32> %row.load67, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %77 = extractelement <5 x i32> %row.load34, i32 4
  %broadcast.insert144 = insertelement <4 x i32> poison, i32 %77, i32 0
  %broadcast145 = shufflevector <4 x i32> %broadcast.insert144, <4 x i32> poison, <4 x i32> zeroinitializer
  %78 = mul <4 x i32> %broadcast145, %block143
  %79 = add <4 x i32> %76, %78
  %80 = shufflevector <4 x i32> %79, <4 x i32> poison, <5 x i32> <i32 0, i32 1, i32 2, i32 3, i32 undef>
  %tile.vect146 = shufflevector <5 x i32> %result.vec.2, <5 x i32> %80, <5 x i32> <i32 5, i32 6, i32 7, i32 8, i32 4>
  %acc.vector147 = add <5 x i32> %result.vec.2, %tile.vect146
  %block148 = shufflevector <5 x i32> %row.load51, <5 x i32> poison, <1 x i32> <i32 4>
  %81 = extractelement <5 x i32> %row.load34, i32 0
  %broadcast.insert149 = insertelement <1 x i32> poison, i32 %81, i32 0
  %broadcast150 = shufflevector <1 x i32> %broadcast.insert149, <1 x i32> poison, <1 x i32> zeroinitializer
  %82 = mul <1 x i32> %broadcast150, %block148
  %block151 = shufflevector <5 x i32> %row.load55, <5 x i32> poison, <1 x i32> <i32 4>
  %83 = extractelement <5 x i32> %row.load34, i32 1
  %broadcast.insert152 = insertelement <1 x i32> poison, i32 %83, i32 0
  %broadcast153 = shufflevector <1 x i32> %broadcast.insert152, <1 x i32> poison, <1 x i32> zeroinitializer
  %84 = mul <1 x i32> %broadcast153, %block151
  %85 = add <1 x i32> %82, %84
  %block154 = shufflevector <5 x i32> %row.load59, <5 x i32> poison, <1 x i32> <i32 4>
  %86 = extractelement <5 x i32> %row.load34, i32 2
  %broadcast.insert155 = insertelement <1 x i32> poison, i32 %86, i32 0
  %broadcast156 = shufflevector <1 x i32> %broadcast.insert155, <1 x i32> poison, <1 x i32> zeroinitializer
  %87 = mul <1 x i32> %broadcast156, %block154
  %88 = add <1 x i32> %85, %87
  %block157 = shufflevector <5 x i32> %row.load63, <5 x i32> poison, <1 x i32> <i32 4>
  %89 = extractelement <5 x i32> %row.load34, i32 3
  %broadcast.insert158 = insertelement <1 x i32> poison, i32 %89, i32 0
  %broadcast159 = shufflevector <1 x i32> %broadcast.insert158, <1 x i32> poison, <1 x i32> zeroinitializer
  %90 = mul <1 x i32> %broadcast159, %block157
  %91 = add <1 x i32> %88, %90
  %block160 = shufflevector <5 x i32> %row.load67, <5 x i32> poison, <1 x i32> <i32 4>
  %92 = extractelement <5 x i32> %row.load34, i32 4
  %broadcast.insert161 = insertelement <1 x i32> poison, i32 %92, i32 0
  %broadcast162 = shufflevector <1 x i32> %broadcast.insert161, <1 x i32> poison, <1 x i32> zeroinitializer
  %93 = mul <1 x i32> %broadcast162, %block160
  %94 = add <1 x i32> %91, %93
  %95 = shufflevector <1 x i32> %94, <1 x i32> poison, <5 x i32> <i32 0, i32 undef, i32 undef, i32 undef, i32 undef>
  %tile.vect163 = shufflevector <5 x i32> %acc.vector147, <5 x i32> %95, <5 x i32> <i32 0, i32 1, i32 2, i32 3, i32 5>
  %acc.vector164 = add <5 x i32> %acc.vector147, %tile.vect163
  %block165 = shufflevector <5 x i32> %row.load51, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %96 = extractelement <5 x i32> %row.load38, i32 0
  %broadcast.insert166 = insertelement <4 x i32> poison, i32 %96, i32 0
  %broadcast167 = shufflevector <4 x i32> %broadcast.insert166, <4 x i32> poison, <4 x i32> zeroinitializer
  %97 = mul <4 x i32> %broadcast167, %block165
  %block168 = shufflevector <5 x i32> %row.load55, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %98 = extractelement <5 x i32> %row.load38, i32 1
  %broadcast.insert169 = insertelement <4 x i32> poison, i32 %98, i32 0
  %broadcast170 = shufflevector <4 x i32> %broadcast.insert169, <4 x i32> poison, <4 x i32> zeroinitializer
  %99 = mul <4 x i32> %broadcast170, %block168
  %100 = add <4 x i32> %97, %99
  %block171 = shufflevector <5 x i32> %row.load59, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %101 = extractelement <5 x i32> %row.load38, i32 2
  %broadcast.insert172 = insertelement <4 x i32> poison, i32 %101, i32 0
  %broadcast173 = shufflevector <4 x i32> %broadcast.insert172, <4 x i32> poison, <4 x i32> zeroinitializer
  %102 = mul <4 x i32> %broadcast173, %block171
  %103 = add <4 x i32> %100, %102
  %block174 = shufflevector <5 x i32> %row.load63, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %104 = extractelement <5 x i32> %row.load38, i32 3
  %broadcast.insert175 = insertelement <4 x i32> poison, i32 %104, i32 0
  %broadcast176 = shufflevector <4 x i32> %broadcast.insert175, <4 x i32> poison, <4 x i32> zeroinitializer
  %105 = mul <4 x i32> %broadcast176, %block174
  %106 = add <4 x i32> %103, %105
  %block177 = shufflevector <5 x i32> %row.load67, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %107 = extractelement <5 x i32> %row.load38, i32 4
  %broadcast.insert178 = insertelement <4 x i32> poison, i32 %107, i32 0
  %broadcast179 = shufflevector <4 x i32> %broadcast.insert178, <4 x i32> poison, <4 x i32> zeroinitializer
  %108 = mul <4 x i32> %broadcast179, %block177
  %109 = add <4 x i32> %106, %108
  %110 = shufflevector <4 x i32> %109, <4 x i32> poison, <5 x i32> <i32 0, i32 1, i32 2, i32 3, i32 undef>
  %tile.vect180 = shufflevector <5 x i32> %result.vec.3, <5 x i32> %110, <5 x i32> <i32 5, i32 6, i32 7, i32 8, i32 4>
  %acc.vector181 = add <5 x i32> %result.vec.3, %tile.vect180
  %block182 = shufflevector <5 x i32> %row.load51, <5 x i32> poison, <1 x i32> <i32 4>
  %111 = extractelement <5 x i32> %row.load38, i32 0
  %broadcast.insert183 = insertelement <1 x i32> poison, i32 %111, i32 0
  %broadcast184 = shufflevector <1 x i32> %broadcast.insert183, <1 x i32> poison, <1 x i32> zeroinitializer
  %112 = mul <1 x i32> %broadcast184, %block182
  %block185 = shufflevector <5 x i32> %row.load55, <5 x i32> poison, <1 x i32> <i32 4>
  %113 = extractelement <5 x i32> %row.load38, i32 1
  %broadcast.insert186 = insertelement <1 x i32> poison, i32 %113, i32 0
  %broadcast187 = shufflevector <1 x i32> %broadcast.insert186, <1 x i32> poison, <1 x i32> zeroinitializer
  %114 = mul <1 x i32> %broadcast187, %block185
  %115 = add <1 x i32> %112, %114
  %block188 = shufflevector <5 x i32> %row.load59, <5 x i32> poison, <1 x i32> <i32 4>
  %116 = extractelement <5 x i32> %row.load38, i32 2
  %broadcast.insert189 = insertelement <1 x i32> poison, i32 %116, i32 0
  %broadcast190 = shufflevector <1 x i32> %broadcast.insert189, <1 x i32> poison, <1 x i32> zeroinitializer
  %117 = mul <1 x i32> %broadcast190, %block188
  %118 = add <1 x i32> %115, %117
  %block191 = shufflevector <5 x i32> %row.load63, <5 x i32> poison, <1 x i32> <i32 4>
  %119 = extractelement <5 x i32> %row.load38, i32 3
  %broadcast.insert192 = insertelement <1 x i32> poison, i32 %119, i32 0
  %broadcast193 = shufflevector <1 x i32> %broadcast.insert192, <1 x i32> poison, <1 x i32> zeroinitializer
  %120 = mul <1 x i32> %broadcast193, %block191
  %121 = add <1 x i32> %118, %120
  %block194 = shufflevector <5 x i32> %row.load67, <5 x i32> poison, <1 x i32> <i32 4>
  %122 = extractelement <5 x i32> %row.load38, i32 4
  %broadcast.insert195 = insertelement <1 x i32> poison, i32 %122, i32 0
  %broadcast196 = shufflevector <1 x i32> %broadcast.insert195, <1 x i32> poison, <1 x i32> zeroinitializer
  %123 = mul <1 x i32> %broadcast196, %block194
  %124 = add <1 x i32> %121, %123
  %125 = shufflevector <1 x i32> %124, <1 x i32> poison, <5 x i32> <i32 0, i32 undef, i32 undef, i32 undef, i32 undef>
  %tile.vect197 = shufflevector <5 x i32> %acc.vector181, <5 x i32> %125, <5 x i32> <i32 0, i32 1, i32 2, i32 3, i32 5>
  %acc.vector198 = add <5 x i32> %acc.vector181, %tile.vect197
  %block199 = shufflevector <5 x i32> %row.load51, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %126 = extractelement <5 x i32> %row.load42, i32 0
  %broadcast.insert200 = insertelement <4 x i32> poison, i32 %126, i32 0
  %broadcast201 = shufflevector <4 x i32> %broadcast.insert200, <4 x i32> poison, <4 x i32> zeroinitializer
  %127 = mul <4 x i32> %broadcast201, %block199
  %block202 = shufflevector <5 x i32> %row.load55, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %128 = extractelement <5 x i32> %row.load42, i32 1
  %broadcast.insert203 = insertelement <4 x i32> poison, i32 %128, i32 0
  %broadcast204 = shufflevector <4 x i32> %broadcast.insert203, <4 x i32> poison, <4 x i32> zeroinitializer
  %129 = mul <4 x i32> %broadcast204, %block202
  %130 = add <4 x i32> %127, %129
  %block205 = shufflevector <5 x i32> %row.load59, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %131 = extractelement <5 x i32> %row.load42, i32 2
  %broadcast.insert206 = insertelement <4 x i32> poison, i32 %131, i32 0
  %broadcast207 = shufflevector <4 x i32> %broadcast.insert206, <4 x i32> poison, <4 x i32> zeroinitializer
  %132 = mul <4 x i32> %broadcast207, %block205
  %133 = add <4 x i32> %130, %132
  %block208 = shufflevector <5 x i32> %row.load63, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %134 = extractelement <5 x i32> %row.load42, i32 3
  %broadcast.insert209 = insertelement <4 x i32> poison, i32 %134, i32 0
  %broadcast210 = shufflevector <4 x i32> %broadcast.insert209, <4 x i32> poison, <4 x i32> zeroinitializer
  %135 = mul <4 x i32> %broadcast210, %block208
  %136 = add <4 x i32> %133, %135
  %block211 = shufflevector <5 x i32> %row.load67, <5 x i32> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %137 = extractelement <5 x i32> %row.load42, i32 4
  %broadcast.insert212 = insertelement <4 x i32> poison, i32 %137, i32 0
  %broadcast213 = shufflevector <4 x i32> %broadcast.insert212, <4 x i32> poison, <4 x i32> zeroinitializer
  %138 = mul <4 x i32> %broadcast213, %block211
  %139 = add <4 x i32> %136, %138
  %140 = shufflevector <4 x i32> %139, <4 x i32> poison, <5 x i32> <i32 0, i32 1, i32 2, i32 3, i32 undef>
  %tile.vect214 = shufflevector <5 x i32> %result.vec.4, <5 x i32> %140, <5 x i32> <i32 5, i32 6, i32 7, i32 8, i32 4>
  %acc.vector215 = add <5 x i32> %result.vec.4, %tile.vect214
  %block216 = shufflevector <5 x i32> %row.load51, <5 x i32> poison, <1 x i32> <i32 4>
  %141 = extractelement <5 x i32> %row.load42, i32 0
  %broadcast.insert217 = insertelement <1 x i32> poison, i32 %141, i32 0
  %broadcast218 = shufflevector <1 x i32> %broadcast.insert217, <1 x i32> poison, <1 x i32> zeroinitializer
  %142 = mul <1 x i32> %broadcast218, %block216
  %block219 = shufflevector <5 x i32> %row.load55, <5 x i32> poison, <1 x i32> <i32 4>
  %143 = extractelement <5 x i32> %row.load42, i32 1
  %broadcast.insert220 = insertelement <1 x i32> poison, i32 %143, i32 0
  %broadcast221 = shufflevector <1 x i32> %broadcast.insert220, <1 x i32> poison, <1 x i32> zeroinitializer
  %144 = mul <1 x i32> %broadcast221, %block219
  %145 = add <1 x i32> %142, %144
  %block222 = shufflevector <5 x i32> %row.load59, <5 x i32> poison, <1 x i32> <i32 4>
  %146 = extractelement <5 x i32> %row.load42, i32 2
  %broadcast.insert223 = insertelement <1 x i32> poison, i32 %146, i32 0
  %broadcast224 = shufflevector <1 x i32> %broadcast.insert223, <1 x i32> poison, <1 x i32> zeroinitializer
  %147 = mul <1 x i32> %broadcast224, %block222
  %148 = add <1 x i32> %145, %147
  %block225 = shufflevector <5 x i32> %row.load63, <5 x i32> poison, <1 x i32> <i32 4>
  %149 = extractelement <5 x i32> %row.load42, i32 3
  %broadcast.insert226 = insertelement <1 x i32> poison, i32 %149, i32 0
  %broadcast227 = shufflevector <1 x i32> %broadcast.insert226, <1 x i32> poison, <1 x i32> zeroinitializer
  %150 = mul <1 x i32> %broadcast227, %block225
  %151 = add <1 x i32> %148, %150
  %block228 = shufflevector <5 x i32> %row.load67, <5 x i32> poison, <1 x i32> <i32 4>
  %152 = extractelement <5 x i32> %row.load42, i32 4
  %broadcast.insert229 = insertelement <1 x i32> poison, i32 %152, i32 0
  %broadcast230 = shufflevector <1 x i32> %broadcast.insert229, <1 x i32> poison, <1 x i32> zeroinitializer
  %153 = mul <1 x i32> %broadcast230, %block228
  %154 = add <1 x i32> %151, %153
  %155 = shufflevector <1 x i32> %154, <1 x i32> poison, <5 x i32> <i32 0, i32 undef, i32 undef, i32 undef, i32 undef>
  %tile.vect231 = shufflevector <5 x i32> %acc.vector215, <5 x i32> %155, <5 x i32> <i32 0, i32 1, i32 2, i32 3, i32 5>
  %acc.vector232 = add <5 x i32> %acc.vector215, %tile.vect231
  br label %loop.latch21

loop.latch21:                                     ; preds = %loop.body
  %loop.step23 = add i32 %loop.iv22, 5
  %loop.step24 = icmp ne i32 %loop.step23, 100
  br i1 %loop.step24, label %loop.header20, label %loop.latch16

loop.latch16:                                     ; preds = %loop.latch21
  %acc.vector96.lcssa = phi <5 x i32> [ %acc.vector96, %loop.latch21 ]
  %acc.vector130.lcssa = phi <5 x i32> [ %acc.vector130, %loop.latch21 ]
  %acc.vector164.lcssa = phi <5 x i32> [ %acc.vector164, %loop.latch21 ]
  %acc.vector198.lcssa = phi <5 x i32> [ %acc.vector198, %loop.latch21 ]
  %acc.vector232.lcssa = phi <5 x i32> [ %acc.vector232, %loop.latch21 ]
  %loop.step18 = add i32 %loop.iv17, 5
  %loop.step19 = icmp ne i32 %loop.step18, 100
  %input.offset234 = add i32 %input.stride233, %loop.iv17
  %input.offset236 = add i32 %input.stride235, %input.offset234
  %input.offset238 = add i32 %input.stride237, %input.offset236
  %tile.start239 = getelementptr i32, i32* %5, i32 %input.offset238
  %vec.cast240 = bitcast i32* %tile.start239 to <5 x i32>*
  store <5 x i32> %acc.vector96.lcssa, <5 x i32>* %vec.cast240, align 4
  %vec.gep242 = getelementptr i32, i32* %tile.start239, i32 100
  %vec.cast243 = bitcast i32* %vec.gep242 to <5 x i32>*
  store <5 x i32> %acc.vector130.lcssa, <5 x i32>* %vec.cast243, align 4
  %vec.gep245 = getelementptr i32, i32* %tile.start239, i32 200
  %vec.cast246 = bitcast i32* %vec.gep245 to <5 x i32>*
  store <5 x i32> %acc.vector164.lcssa, <5 x i32>* %vec.cast246, align 4
  %vec.gep248 = getelementptr i32, i32* %tile.start239, i32 300
  %vec.cast249 = bitcast i32* %vec.gep248 to <5 x i32>*
  store <5 x i32> %acc.vector198.lcssa, <5 x i32>* %vec.cast249, align 4
  %vec.gep251 = getelementptr i32, i32* %tile.start239, i32 400
  %vec.cast252 = bitcast i32* %vec.gep251 to <5 x i32>*
  store <5 x i32> %acc.vector232.lcssa, <5 x i32>* %vec.cast252, align 4
  br i1 %loop.step19, label %loop.header15, label %loop.latch11

loop.latch11:                                     ; preds = %loop.latch16
  %loop.step13 = add i32 %loop.iv12, 5
  %loop.step14 = icmp ne i32 %loop.step13, 100
  br i1 %loop.step14, label %loop.header10, label %loop.latch6

loop.latch6:                                      ; preds = %loop.latch11
  %loop.step8 = add i32 %loop.iv7, 1
  %loop.step9 = icmp ne i32 %loop.step8, 1
  br i1 %loop.step9, label %loop.header5, label %loop.latch

loop.latch:                                       ; preds = %loop.latch6
  %loop.step = add i32 %loop.iv, 1
  %loop.step4 = icmp ne i32 %loop.step, 1
  br i1 %loop.step4, label %loop.header, label %continue

continue:                                         ; preds = %loop.latch
  %malloc.cast = bitcast i32* %5 to <10000 x i32>*
  %final.load = load <10000 x i32>, <10000 x i32>* %malloc.cast, align 1
  ret <10000 x i32> %final.load
}

declare dso_local i32 @tensor_typeinfo10000(<10000 x i32>* byval(<10000 x i32>) align 65536, <4 x i32>, <4 x i32>, <4 x i32>) #1

declare dso_local <10000 x i32> @tensor_matmul10000(i32, i32) #1

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @main() #0 {
  %1 = alloca <10000 x i32>, align 65536
  %2 = alloca <10000 x i32>, align 65536
  store <10000 x i32> zeroinitializer, <10000 x i32>* %1, align 65536
  store <10000 x i32> zeroinitializer, <10000 x i32>* %2, align 65536
  %3 = call <10000 x i32> @foo(<10000 x i32>* byval(<10000 x i32>) align 65536 %1, <10000 x i32>* byval(<10000 x i32>) align 65536 %2)
  ret i32 0
}

; Function Attrs: nounwind readnone speculatable
declare token @llvm.tensor.typeinfo.p0v10000i32.v4i32.v4i32.v4i32(<10000 x i32>*, <4 x i32>, <4 x i32>, <4 x i32>) #2

; Function Attrs: nounwind readnone speculatable
declare <10000 x i32> @llvm.tensor.matmul.v10000i32(token, token) #2

declare noalias i8* @malloc(i32)

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.memcpy.p0i8.p0i8.i32(i8* noalias nocapture writeonly, i8* noalias nocapture readonly, i32, i1 immarg) #3

attributes #0 = { noinline nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "min-legal-vector-width"="320000" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nounwind readnone speculatable }
attributes #3 = { argmemonly nofree nosync nounwind willreturn }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 12.0.0 (https://github.com/AkashIwnK/tensor-codegen.git 7b8ef7eb12f8506d4ad3a147199a39d693aa32b5)"}
