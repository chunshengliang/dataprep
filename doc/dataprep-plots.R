## ----include = FALSE----------------------------------------------------------
knitr::opts_chunk$set(
  collapse  = TRUE,
  comment   = "#>",
  fig.align = "center",
  fig.width = 7,
  fig.height = 4.5,
  out.width = "95%"
)

## -----------------------------------------------------------------------------
library(dataprep)
library(ggplot2)

## -----------------------------------------------------------------------------
descplot(data, cols = 5:65)

## -----------------------------------------------------------------------------
descplot(data1, cols = 3:7) +
  ggplot2::theme(
    axis.text.x = ggplot2::element_text(angle = 30,
                                        hjust = 1, vjust = 1.1))

## -----------------------------------------------------------------------------
descplot(data, cols = 5:65,
         stats = c("na", "min", "max", "IQR"))

## -----------------------------------------------------------------------------
descplot(data1, cols = 3:7, stats = c("min", "max", "IQR"),
         ncol = 2) +
  ggplot2::theme(
    axis.text.x = ggplot2::element_text(angle = 30,
                                        hjust = 1, vjust = 1.1))

## -----------------------------------------------------------------------------
descdata(data1, cols = 3:7, stats = c(2, 3, 4, 7:9))

## -----------------------------------------------------------------------------
percplot(data, cols = 5:65, group = 4)

## -----------------------------------------------------------------------------
pct <- percdata(data, cols = 5:65, group = 4)
sum(is.na(pct[, -(1:2), drop = FALSE]))

## -----------------------------------------------------------------------------
percplot(data, cols = 27:61, group = 4)

## -----------------------------------------------------------------------------
percplot(data, cols = 27:61, group = 4, part = "top")

## -----------------------------------------------------------------------------
percplot(data, cols = 27:61, group = 4, part = "bottom")

## -----------------------------------------------------------------------------
percplot(data1, cols = 3:7, group = 2) +
  ggplot2::theme(
    axis.text.x = ggplot2::element_text(angle = 30,
                                        hjust = 1, vjust = 1.1))

## -----------------------------------------------------------------------------
percplot(data, cols = 27:61, group = 4, num_xaxis = "numeric")

## -----------------------------------------------------------------------------
percdata(data1, cols = 3:7, group = 2, part = "top")

## ----eval = FALSE-------------------------------------------------------------
# # 1. Overview of the whole table
# data_report(data, cols = 5:65)
# 
# # 2. Per-column NA run statistics
# na_diagnose(data, cols = 5:65)
# 
# # 3. Descriptive statistics of the raw data
# descplot(data, cols = 5:65)
# 
# # 4. Percentile plots of the raw data
# percplot(data, cols = 5:65, group = 4)

## ----eval = FALSE-------------------------------------------------------------
# cleaned <- dataprep(data, cols = 5:65, group = 4)
# 
# percplot(
#   rbind(
#     transform(data[names(cleaned)], g = "original"),
#     transform(cleaned,               g = "preprocessed")
#   ),
#   cols  = 5:ncol(cleaned),
#   group = ncol(cleaned) + 1
# )

## -----------------------------------------------------------------------------
sessionInfo()

