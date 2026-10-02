-- Counter: café °C header (Latin-1)
-- second header line
entity Counter is
  generic (W : natural := 8);
  port (clk : in std_logic; q : out std_logic_vector(W-1 downto 0));
end entity Counter;

architecture rtl of counter is
  /* block -- comment
     spanning lines */
  signal \My Sig\ : bit;
begin
  q <= (others => '0') when clk'event and x"FF" ?= 8x"ff" else std_logic_vector'(q);
end architecture;
