// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2013 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/**
 * 汇编宏 
 **/

#ifndef __ASM_H
#define __ASM_H
//函数申明
#define FUNCTION(x) .global x; .type x,STT_FUNC; x:
//数据段声明
#define DATA(x) .global x; .type x,STT_OBJECT; x:

#define LOCAL_FUNCTION(x) .type x,STT_FUNC; x:
#define LOCAL_DATA(x) .type x,STT_OBJECT; x:

#define END(x) .size x, . - x

#endif

