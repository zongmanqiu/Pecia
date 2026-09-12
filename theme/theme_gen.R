# OKLAB颜色空间，指定主颜色，以及两两颜色关系，迭代出最优组合。

# 全局优化包
library(GA)

# RGB参数转换为OKLAB的L/a/b参数
# OKLAB: https://bottosson.github.io/posts/oklab/
# Linear RGB: Color Space Transformation-Based Smartphone Algorithm for Colorimetric Urinalysis
sRGB2OKLAB <- function(rgb) {
  # 提取参数
  r <- rgb[1]; g <- rgb[2]; b <- rgb[3]
  
  # sRGB -> 线性RGB, 逆gamma校正
  s2lin <- function(c) { 
    c <- c/255
    ifelse(c <= 0.04045, c/12.92, ((c+0.055)/1.055)^2.4) 
  }
  
  # 初始参数的线性转换
  R <- s2lin(r); G <- s2lin(g); B <- s2lin(b)
  
  # 线性RGB -> XYZ
  X <- 0.4124*R + 0.3576*G + 0.1805*B
  Y <- 0.2126*R + 0.7152*G + 0.0722*B
  Z <- 0.0193*R + 0.1192*G + 0.9505*B
  
  # LMS: M1*XYZ
  L1 <- 0.8189330101*X + 0.3618667424*Y - 0.1288597137*Z
  M1 <- 0.0329845436*X + 0.9293118715*Y + 0.0361456387*Z
  S1 <- 0.0482003018*X + 0.2643662691*Y + 0.6338517070*Z
  
  # LMS': LMS^(1/3)
  L2 <- sign(L1)*abs(L1)^(1/3)
  M2 <- sign(M1)*abs(M1)^(1/3)
  S2 <- sign(S1)*abs(S1)^(1/3)
  
  # OKLAB: M2*LMS'
  L <- 0.2104542553*L2 + 0.7936177850*M2 - 0.0040720468*S2
  a <- 1.9779984951*L2 - 2.4285922050*M2 + 0.4505937099*S2
  b <- 0.0259040371*L2 + 0.7827717662*M2 - 0.8086757660*S2
  
  # 输出
  c(L, a, b)
}


# OKLAB参数转换为明度/彩度/色相
OKLAB2LCH <- function(Lab) {
  # 提取参数 
  L <- Lab[1]; a <- Lab[2]; b <- Lab[3]
  C <- sqrt(a^2 + b^2)
  H <- atan2(b, a) * 180 / pi
  if (H < 0) H <- H + 360
  c(L, C, H)
}


# 输入n行3列的rgb参数矩阵，自动计算L/C/H的差值矩阵
# 矩阵下三角为差值绝对值，其余为0
rgbM2LCH <- function(rgbM){
  # 批量计算颜色的LCH参数
  ColorLCH <- t(apply(rgbM, 1, function(r) OKLAB2LCH(sRGB2OKLAB(r)) ))
  
  # L/C/H差异矩阵
  n <- nrow(rgbM)
  LD <- CD <- HD <- matrix(0, n, n)
  
  # 循环计算两两差异，输入矩阵下三角
  # 全部采用归一化策略
  for (i in 2:n){
    for(p in 1:(i-1)){
      LD[i, p] <- abs(ColorLCH[i,1] - ColorLCH[p,1])
      CD[i, p] <- abs(ColorLCH[i,2] - ColorLCH[p,2])/0.4
      Hdiff    <- abs(ColorLCH[i,3] - ColorLCH[p,3])
      HD[i, p] <- min(Hdiff, 360-Hdiff)/180
    }
  }
  
  return( list(LD = LD, CD = CD, HD = HD) )
}

# 目标损失函数
target <- function(para, rgb, n, L, C, H){
  # 组合成RGB参数矩阵
  rgbM <- matrix(c(rgb,para), n, 3, byrow=T)
  
  # 计算两两L/C/H差值矩阵
  LCH <- rgbM2LCH(rgbM)
  
  # 计算矩阵差值平方
  LD2 <- (L-LCH$LD)^2
  
  # 同过矩阵运算，快速计算所有颜色对差值与目标差值的平方
  # L矩阵下三角是目标差值，上三角是权重，对角线是0
  MLD <- sum(LD2*t(LD2))/(n^2-n)
  
  # CD2 和 HD2 同 LD2
  CD2 <- (C-LCH$CD)^2
  MCD <- sum(CD2*t(CD2))/(n^2-n)
  HD2 <- (H-LCH$HD)^2
  MHD <- sum(HD2*t(HD2))/(n^2-n)
  
  # 最终的损失总和，三者等权？
  return(MLD+MCD+MHD)
}



########## 准备分析

##### 当前方案

# 总颜色个数
n <- 8

# 颜色参数
rgb <- matrix(c(255,255,255,  # 第一个颜色是主色，固定色，背景1
                0,0,0,        # 文本1
                140,140,140,  # 文本2
                225,225,225,  # 背景2
                240,240,240,  # 背景3
                230,255,230,  # 高亮1
                150,255,200,  # 高亮2
                255,100,100   # 高亮3
                ), 
              n, 3, byrow=T) 

# 明度，明亮程度
L <- matrix(0,n,n)
L[upper.tri(L)] <- 1 # 上三角填充

# 彩度，鲜艳程度
C <- L

# 色相，接近程度
H <- L

# 计算当前的结果
fit1 <- rgbM2LCH(rgb)

# 重新计算 L/C/H
L <- L + fit1$LD
C <- C + fit1$CD
H <- H + fit1$HD



##### 当前方案2，计算亮/暗两套方案，取均值

rgb2 <- matrix(c(0,0,0,  # 第一个颜色是主色，固定色，背景1
                255,255,255,        # 文本1
                115,115,115,  # 文本2
                50,50,50,  # 背景2
                30,30,30,  # 背景3
                50,20,50,  # 高亮1
                105,0,55,  # 高亮2
                0,155,155   # 高亮3
                ), 
               n, 3, byrow=T) 

# 明度，明亮程度
L2 <- matrix(0,n,n)
L2[upper.tri(L2)] <- 1 # 上三角填充

# 彩度，鲜艳程度
C2 <- L2

# 色相，接近程度
H2 <- L2

# 计算当前的结果
fit2 <- rgbM2LCH(rgb2)

# 重新计算 L/C/H
L2 <- L2 + fit2$LD
C2 <- C2 + fit2$CD
H2 <- H2 + fit2$HD


# 综合两套方案
L <- (L+L2)/2
C <- (C+C2)/2
H <- (H+H2)/2


##### 更改主色，迭代出其它颜色

# ---------- 1. 指定新的主色（例如改为红色） ----------
new_main <- c(204,236,255)   # 您可以随意修改此RGB值

# ---------- 2. 准备优化参数 ----------
# 待优化的是除主色外的其余 (n-1) 个颜色，每个颜色3个通道
n_para <- (n - 1) * 3
lower <- rep(0, n_para)
upper <- rep(255, n_para)

# 适应度函数：最大化负损失（因为 ga 默认最大化）
fitness_func <- function(para) {
  -target(para, new_main, n, L, C, H)
}

# ---------- 3. 运行遗传算法 ----------
set.seed(123) # 设置种子

# 建议提供几个优质的初始解（基于原配色的微调）
suggest_seeds <- matrix(
  c( as.vector(rgb[-1, ]), 
     as.vector(rgb[-1, ]) + rnorm(21, 0, 5),
     as.vector(rgb[-1, ]) + rnorm(21, 0, 10) ),
  nrow = 3, byrow = TRUE
)
# 截断到 0-255
suggest_seeds[suggest_seeds < 0] <- 0
suggest_seeds[suggest_seeds > 255] <- 255

ga_result <- ga(
  type = "real-valued",
  fitness = fitness_func,
  lower = lower,
  upper = upper,
  popSize = 1000,            # 2000 降到 1500，省下时间给 optim
  maxiter = 400,             # 400 代 + optim 局部搜索，比 500 代效果好
  run = 150,                 # 增加停滞期，避免早期优秀个体被误杀
  pcrossover = 0.9,
  pmutation = 0.03,
  elitism = floor(1500 * 0.05), # 默认 5% 精英保留
  optim = TRUE,                  # 开启混合优化！
  optimArgs = list(
    method = "L-BFGS-B",
    poptim = 0.05,
    pressel = 0.5,
    control = list(fnscale = -1, maxit = 50)
  ),
  monitor = TRUE
)

summary(ga_result)

# ---------- 4. 提取最优解并取整 ----------
best_para <- ga_result@solution[1, ]
new_rgb_matrix <- matrix(c(new_main, best_para), n, 3, byrow = TRUE)
new_rgb_matrix <- round(new_rgb_matrix)
new_rgb_matrix[new_rgb_matrix < 0] <- 0
new_rgb_matrix[new_rgb_matrix > 255] <- 255

# ---------- 5. 输出结果 ----------
cat("\n===== 新配色（RGB） =====\n")
print(new_rgb_matrix)

# （可选）查看新配色与目标差异的逼近程度
new_fit <- rgbM2LCH(new_rgb_matrix)
cat("\n目标 L 差异矩阵（下三角）:\n")
print(L * lower.tri(L))
cat("\n新配色实际 L 差异矩阵（下三角）:\n")
print(new_fit$LD)

