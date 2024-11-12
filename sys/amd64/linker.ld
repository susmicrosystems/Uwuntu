ENTRY(_start)
 
SECTIONS
{
	. = 0x8000;

	.boot.text BLOCK(4K) : ALIGN(4K)
	{
		*(.boot.text)
		*(.boot.text.*)
	}

	.boot.data BLOCK(4K) : ALIGN(4K)
	{
		*(.boot.data)
	}

	.boot.bss BLOCK(4K) : ALIGN(4K)
	{
		*(.boot.bss)
	}

	. = 1M;

	.multiboot.text BLOCK(4K) : ALIGN(4K)
	{
		*(.multiboot.text)
		*(.multiboot.text.*)
	}

	.multiboot.data BLOCK(4K) : ALIGN(4K)
	{
		*(.multiboot.data)
	}

	.multiboot.bss BLOCK(4K) : ALIGN(4K)
	{
		*(.multiboot.bss)
	}

	. += 0xFFFFFFFF80000000;

	_kernel_begin = .;

	.text BLOCK(4K) : AT(ADDR(.text) - 0xFFFFFFFF80000000)
	{
		*(.text)
		*(.text.*)
	}

	.rodata BLOCK(4K) : AT(ADDR(.rodata) - 0xFFFFFFFF80000000)
	{
		*(.rodata*)
	}

	.data BLOCK(4K) : AT(ADDR(.data) - 0xFFFFFFFF80000000)
	{
		*.o(.data)
	}

	.bss BLOCK(4K) : AT(ADDR(.bss) - 0xFFFFFFFF80000000)
	{
		*(COMMON)
		*(.bss)
	}

	/* XXX use a symbol here to avoid excessive pages in boot.S */

	.data_rdinit BLOCK(4K) : AT(ADDR(.data_rdinit) - 0xFFFFFFFF80000000)
	{
		*.tar(.data)
	}

	. = ALIGN(4K);
	_kernel_end = .;
}
