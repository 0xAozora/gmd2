package types

const (
	OK                 int32 = 0
	ErrInvalidArgument int32 = -1
	ErrInvalidLayout   int32 = -2
	ErrInvalidStream   int32 = -3
	ErrOutputTooSmall  int32 = -4
	ErrScratchTooSmall int32 = -5
	ErrChannelMismatch int32 = -6
)
